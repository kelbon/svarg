#include "svarg/utils/media_types.hpp"

namespace svarg::noexport {
// parameters      = *( OWS ";" OWS [ parameter ] )
// parameter       = parameter-name "=" parameter-value
// parameter-name  = token
// parameter-value = ( token / quoted-string )
static std::pair<std::string_view, std::string_view> parse_parameter(std::string_view param) {
  std::string_view name = noexport::eat_token(param);
  if (!param.starts_with('='))
    throw validate_error("parameter must be with =");
  std::string_view value = param.substr(1);  // rm '='
  if (value.empty())
    throw validate_error("parameter value must not be empty, param: `{}`", param);
  noexport::unquote(value);
  return {name, value};
}

static bool has_case_insensitive_value(std::string_view s) noexcept {
  assert(is_lowered(s));
  // теоретически это зависит от параметра и его семантикИ, но нигде нет списка ни параметров ни их семантики,
  // поэтому эта еботека сводится к обрабатыванию только charset
  return s == "charset";
}

static std::string_view split_media_type_and_params(std::string_view s, std::string_view& params) {
  auto pos = s.find(';');
  if (pos == s.npos) [[likely]] {
    // не допускает пробелов ни до ни после
    return s;
  }
  // включая ';'
  // parameters = *( OWS ";" OWS [ parameter ] )
  params = s.substr(pos);
  std::string_view result = s.substr(0, pos);
  // по грамматике пробелы допустимы только перед ';'
  noexport::trim_qws_back(result);
  return result;
}

static bool needs_quoting(const std::string& value) {
  if (value.empty()) {
    return true;
  }
  for (unsigned char c : value) {
    if (!noexport::is_tchar(static_cast<char>(c))) {
      return true;
    }
  }
  return false;
}

static void quoted_into(std::string_view value, std::string& out) {
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (char c : value) {
    if (c == '"' || c == '\\')
      out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
}
// quoted-string = DQUOTE *( qdtext / quoted-pair ) DQUOTE
// Возвращает участок вместе с кавычками, сдвигая `in` за его пределы.
static std::string_view eat_quoted_string(std::string_view& in) {
  assert(in.starts_with('"'));
  size_t pos = 1;
  for (; pos < in.size(); ++pos) {
    if (in[pos] == '\\') {
      if (++pos >= in.size())
        throw validate_error("invalid escape in quoted-string `{}`", in);
    } else if (in[pos] == '"') {
      std::string_view result = in.substr(0, ++pos);
      in.remove_prefix(pos);
      return result;
    }
  }
  throw validate_error("unterminated quoted-string `{}`", in);
}

static std::string unquote_value(std::string_view quoted) {
  noexport::unquote(quoted);
  if (quoted.find('\\') == std::string_view::npos)
    return std::string(quoted);
  std::string out;
  out.reserve(quoted.size());
  for (size_t i = 0; i < quoted.size(); ++i) {
    if (quoted[i] == '\\' && i + 1 < quoted.size())
      ++i;
    out.push_back(quoted[i]);
  }
  return out;
}

}  // namespace svarg::noexport

namespace svarg {

std::strong_ordering compare_media_types(std::string_view a, std::string_view b) {
  assert(a == monomorph_media_type(a) && b == monomorph_media_type(b));

  // игнорируем параметры
  std::string_view name_value_a = a.substr(0, a.find(';'));
  std::string_view name_value_b = b.substr(0, b.find(';'));

  // это довольно странно, но это правильная логика
  // всего случаев 3 - 2 звезды, 1 звезда и 0
  // 0 означает что это просто медиа тип - он более специализирован чем любой ренж и */*
  // в ренже может быть либо */* - две звезды, либо type/* - но никогда не больше одного /
  // итого */* менее специализрован чем любой ренж (одна звезда), любой ренж менее специализирован чем без
  // звёзд

  // теоретически можно сравнивать параметры как множество и тд, но как-то плевать
  size_t astarts = noexport::count_star_segments(name_value_a);
  size_t bstarts = noexport::count_star_segments(name_value_b);
  if (astarts > 2 || bstarts > 2)
    throw validate_error("invalid media type `{}` or `{}`, too many stars", a, b);
  if (astarts == bstarts)
    // сравниваем учитывая параметры.
    //  больше параметров - более специализирован (без заморочек про значения и ключи)
    return a <=> b;
  // меньше звёзд - более специализирован
  return bstarts <=> astarts;
}

// грамматика:
//   media-type = type "/" subtype parameters
//   type       = token
//   subtype    = token
// "The type and subtype tokens are case-insensitive"
std::string monomorph_media_type(std::string_view input) {
  const std::string_view unchanged_input = input;
  std::string out;

  std::string_view type = noexport::eat_token(input);
  if (input.empty() || input.front() != '/')
    throw validate_error("invalid media type `{}`, no required '/' found", unchanged_input);
  input.remove_prefix(1);  // skip '/'
  std::string_view subtype = noexport::eat_token(input);
  noexport::trim_qws_front(input);
  out += lowered(type);
  out += '/';
  out += lowered(subtype);

  if (input.empty()) [[likely]]  // нет параметров
    return out;
  if (input.front() != ';')
    throw validate_error("invalid media type `{}`, parameters should start with `;`", unchanged_input);
  input.remove_prefix(1);  // skip ';'

  // параметры сортируются по имени
  std::map<std::string, std::string> parameters;
  while (!input.empty()) {
    noexport::trim_qws_front(input);
    if (input.empty())
      break;  // оканчивается на ";  "
    auto next = input.find(';');
    std::string_view param = input.substr(0, next);
    noexport::trim_qws_back(param);
    auto [pname, pvalue] = noexport::parse_parameter(param);
    std::string name = lowered(pname);
    // https://www.rfc-editor.org/rfc/rfc9110.html#name-quality-values
    // q параметр выкидывается. Он может встречаться только в accept, а здесь не значит ничего
    if (pname != "q")
      parameters[name] = noexport::has_case_insensitive_value(name) ? lowered(pvalue) : pvalue;
    if (next == std::string_view::npos)
      break;
    input.remove_prefix(next + 1);
  }
  for (auto& [name, value] : parameters)
    out += std::format("; {}={}", name, value);
  return out;
}

bool match_content_type(std::string_view content_type, std::string_view candidate) {
  assert(monomorph_media_type(content_type) == content_type);
  assert(without_media_type_parameters(content_type) == content_type);
  assert(!is_wildcard_content_type(candidate));
  auto ends_or_params = [&c = candidate, &t = content_type] {
    // проверка, что кандидат оканчивается сразу после content_type (либо идут параметры)
    // лямбдой отложено после && чтобы не всегда проверять
    // нужно чтобы "application/json" не матчилось с "application/json2" через starts_with
    return c.size() == t.size() || (noexport::is_qws(c[t.size()]) || c[t.size()] == ';');
  };
  switch (noexport::count_star_segments(content_type)) {
    case 2:
      // */*
      return true;
    case 1:
      assert(content_type.ends_with("/*"));  // ренжи всегда заканчиваются только так
      content_type.remove_suffix(2);
      return starts_with_ci_prefix_lowered(candidate, content_type);
    case 0:
      return starts_with_ci_prefix_lowered(candidate, content_type) && ends_or_params();
  }
  metel::unreachable();
}

std::string_view without_media_type_parameters(std::string_view s) {
  std::string_view result = s.substr(0, s.find(';'));
  noexport::trim_qws(result);
  return result;
}

/*
The encoding object SHALL only apply to requestBody objects
when the media type is multipart or application/x-www-form-urlencoded
*/
bool media_type_supports_encodings(std::string_view media_type) {
  std::string mt = monomorph_media_type(media_type);
  return mt.starts_with("multipart/") || mt == "application/x-www-form-urlencoded";
}

std::string media_type_to_hdr_value(std::string_view media_type, const content_type_params& params) {
  std::string result(media_type);

  for (const auto& [name, value] : params) {
    result += "; ";
    result += name;
    result += '=';
    if (noexport::needs_quoting(value)) {
      noexport::quoted_into(value, result);
    } else {
      result += value;
    }
  }

  return result;
}

std::string_view media_type_from_hdr_value(std::string_view content_type_value, content_type_params& out) {
  std::string_view params;
  std::string_view mt = noexport::split_media_type_and_params(content_type_value, params);

  if (params.empty()) [[likely]]
    return mt;
  // parameters = *( OWS ";" OWS [ parameter ] )
  for (;;) {
    noexport::trim_qws_front(params);
    if (params.empty())
      return mt;
    if (params.front() != ';')
      throw validate_error("expected ';' in `{}`", content_type_value);
    params.remove_prefix(1);  // ;
    noexport::trim_qws_front(params);
    if (params.empty())
      break;  // висячий ';' в конце допустим

    std::string_view name = noexport::eat_token(params);
    if (!params.starts_with('='))
      throw validate_error("expected '=' after `{}`", name);
    params.remove_prefix(1);  // =

    std::string value = params.starts_with('"') ? noexport::unquote_value(noexport::eat_quoted_string(params))
                                                : std::string(noexport::eat_token(params));
    out.try_emplace(std::string(name), std::move(value));  // дубликаты: первый побеждает
  }
  return mt;
}

}  // namespace svarg
