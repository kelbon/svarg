#include "svarg/utils/path_tools.hpp"

namespace svarg {

std::string path_to_name(std::string_view path) {
  std::string result;
  if (!path.starts_with('/'))
    throw parse_error("path must start with /");
  if (path == "/")
    return "root";
  if (path[1] >= '0' && path[1] <= '9')
    result += '_';

  visit_path_segments(path, [&](std::string_view segment) {
    if (segment.starts_with('{')) {
      if (!segment.ends_with('}'))
        throw parse_error("invalid path segment `{}`", segment);
      segment.remove_prefix(1);
      segment.remove_suffix(1);
    }
    if (segment.empty())
      return;
    result += to_upper_ascii(segment[0]);
    result += segment.substr(1);
  });

  return result;
}

std::string path_to_name(std::string_view path, string_map<std::string>& name2path,
                         string_map<std::string>& path2name) {
  if (auto it = path2name.find(path); it != path2name.end())
    return it->second;

  std::string name = path_to_name(path);

  on_scope_exit {
    path_as_cxx_identifier(name);
    name2path[name] = path;
    path2name[std::string(path)] = name;
  };

  if (!path2name.contains(name))
    return name;
  for (int i = 2;; ++i) {
    // TODO string view rope (типа две строки эмулируют одну строку)
    // TODO ренж строкового представления чисел типа '1' '2' и тд
    auto num = std::to_string(i);
    if (!path2name.contains(name + num)) {
      name += num;
      return name;
    }
  }
}

// https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.2
//  token          = 1*tchar
//
//  tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//                 / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//                 / DIGIT / ALPHA
//                 ; any VCHAR, except delimiters
static constexpr bool is_tchar(char c) noexcept {
  // ALPHA
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    return true;
  // DIGIT
  if (c >= '0' && c <= '9')
    return true;
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

static size_t count_star_segments(std::string_view x) {
  if (x == "*/*")
    return 2;
  // сегмент не может оказаться в середине по грамматике
  // при этом внури сегмента звёздочка оказаться может, поэтому просто count не подойдёт
  if (x.ends_with("/*"))
    return 1;
  return 0;
}

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
  size_t astarts = count_star_segments(name_value_a);
  size_t bstarts = count_star_segments(name_value_b);
  if (astarts > 2 || bstarts > 2)
    throw validate_error("invalid media type `{}` or `{}`, too many stars", a, b);
  if (astarts == bstarts)
    // сравниваем учитывая параметры.
    //  больше параметров - более специализирован (без заморочек про значения и ключи)
    return a <=> b;
  // меньше звёзд - более специализирован
  return bstarts <=> astarts;
}

// https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.3
// HTAB (horizontal tab)
// SP (space)
//   OWS            = *( SP / HTAB )
//                 ; optional whitespace
static bool is_qws(char c) noexcept {
  return c == ' ' || c == '\t';
}

static void trim_front(std::string_view& s) {
  while (!s.empty() && is_qws(s.front()))
    s.remove_prefix(1);
}

static void trim_back(std::string_view& s) {
  while (!s.empty() && is_qws(s.back()))
    s.remove_suffix(1);
}

static void trim(std::string_view& s) {
  trim_front(s);
  trim_back(s);
}

void unquote(std::string_view& s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    s.remove_prefix(1), s.remove_suffix(1);
}

static std::string_view eat_token(std::string_view& in) {
  if (in.empty() || !is_tchar(in.front()))
    throw validate_error("invalid http token `{}`", in);
  const char* p = in.data();
  size_t pos = 0;
  while (!in.empty() && is_tchar(in.front())) {
    ++pos;
    in.remove_prefix(1);
  }
  return std::string_view(p, pos);
}

// parameters      = *( OWS ";" OWS [ parameter ] )
// parameter       = parameter-name "=" parameter-value
// parameter-name  = token
// parameter-value = ( token / quoted-string )
static std::pair<std::string_view, std::string_view> parse_parameter(std::string_view param) {
  std::string_view name = eat_token(param);
  if (!param.starts_with('='))
    throw validate_error("parameter must be with =");
  std::string_view value = param.substr(1);  // rm '='
  if (value.empty())
    throw validate_error("parameter value must not be empty, param: `{}`", param);
  unquote(value);
  return {name, value};
}

static bool has_case_insensitive_value(std::string_view s) noexcept {
  assert(lowered(s) == s);
  // теоретически это зависит от параметра и его семантикИ, но нигде нет списка ни параметров ни их семантики,
  // поэтому эта еботека сводится к обрабатыванию только charset
  return s == "charset";
}

// грамматика:
//   media-type = type "/" subtype parameters
//   type       = token
//   subtype    = token
// "The type and subtype tokens are case-insensitive"
std::string monomorph_media_type(std::string_view input) {
  const std::string_view unchanged_input = input;
  std::string out;

  std::string_view type = eat_token(input);
  if (input.empty() || input.front() != '/')
    throw validate_error("invalid media type `{}`, no required '/' found", unchanged_input);
  input.remove_prefix(1);  // skip '/'
  std::string_view subtype = eat_token(input);
  trim_front(input);
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
    trim_front(input);
    if (input.empty())
      break;  // оканчивается на ";  "
    auto next = input.find(';');
    std::string_view param = input.substr(0, next);
    trim_back(param);
    auto [pname, pvalue] = parse_parameter(param);
    std::string name = lowered(pname);
    // https://www.rfc-editor.org/rfc/rfc9110.html#name-quality-values
    // q параметр выкидывается. Он может встречаться только в accept, а здесь не значит ничего
    if (pname != "q")
      parameters[name] = has_case_insensitive_value(name) ? lowered(pvalue) : pvalue;
    if (next == std::string_view::npos)
      break;
    input.remove_prefix(next + 1);
  }
  for (auto& [name, value] : parameters)
    out += std::format("; {}={}", name, value);
  return out;
}

bool match_content_type(std::string_view candidate, std::string_view content_type) {
  assert(monomorph_media_type(content_type) == content_type);
  assert(without_media_type_parameters(content_type) == content_type);
  switch (count_star_segments(content_type)) {
    case 2:
      return true;
    case 1:
      assert(content_type.ends_with("/*"));  // ренжи всегда заканчиваются только так
      content_type.remove_suffix(2);
      return starts_with_ci_prefix_lowered(candidate, content_type);
    case 0:
      // здесь compare_ci делает то же самое, что monomorph_media_type
      return compare_ci(without_media_type_parameters(candidate), content_type) ==
             std::strong_ordering::equal;
  }
  std::unreachable();
}

std::string_view without_media_type_parameters(std::string_view s) {
  std::string_view result = s.substr(0, s.find(';'));
  trim(result);
  return result;
}

bool match_status_range(int status, status_range_e r) noexcept {
  switch (r) {
    case status_range_e::_1XX:
      return status >= 100 && status < 200;
    case status_range_e::_2XX:
      return status >= 200 && status < 300;
    case status_range_e::_3XX:
      return status >= 300 && status < 400;
    case status_range_e::_4XX:
      return status >= 400 && status < 500;
    case status_range_e::_5XX:
      return status >= 500 && status < 600;
  }
  return false;
}

}  // namespace svarg
