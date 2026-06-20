
#include <string>
#include <string_view>

#include <strswitch/strswitch.hpp>

namespace svarg::cxx {

using namespace std::string_view_literals;

// Все utf8 имеют старший бит установлен, т.е. >= 0x80.
// ASCII байты всегда < 0x80, поэтому пересечений нет.
static constexpr bool is_utf8_multibyte(char c) {
  return static_cast<unsigned char>(c) >= 0x80;
}

static constexpr bool is_digit(char c) noexcept {
  return c >= '0' && c <= '9';
}

// Символы, допустимые в середине/конце идентификатора.
// 'Z' исключён — он используется как escape-префикс.
static constexpr bool is_ident_continue_no_z(char c) {
  switch (c) {
    case 'a' ... 'z':
    case 'A' ... 'Y':  // Z исключено
    case '0' ... '9':
    case '_':
      return true;
    default:
      return false;
  }
}

// Символы, допустимые в середине/конце идентификатора.
static constexpr bool is_ident_continue(char c) {
  switch (c) {
    case 'a' ... 'z':
    case 'A' ... 'Z':  // Z исключено
    case '0' ... '9':
    case '_':
      return true;
    default:
      return false;
  }
}

// Таблица коротких escape-кодов для популярных спецсимволов.
// Z9 зарезервирован для самого 'Z', поэтому коды Z0-Z8 — под спецсимволы.
static constexpr std::string_view escape_char(char c) {
  switch (c) {
    case '-':
      return "Z0";
    case '.':
      return "Z1";
    case '/':
      return "Z2";
    case ' ':
      return "Z3";
    case ':':
      return "Z4";
    case '@':
      return "Z5";
    case '*':
      return "Z6";
    case '[':
      return "Z7";
    case ']':
      return "Z8";
    case 'Z':
      return "Z9";
    default:
      return {};
  }
}

static constexpr char hex_digit(unsigned x) {
  return "0123456789abcdef"[x & 0xF];
}

// Fallback для ASCII символов вне таблицы: Zxhh, где hh — hex-код байта.
static std::string escape_char_fallback(char c) {
  auto b = static_cast<unsigned char>(c);
  return {'Z', 'x', hex_digit(b >> 4), hex_digit(b)};
}

static constexpr bool is_cxx_keyword(std::string_view s) noexcept {
  return ss::string_switch<bool>(s)
      .cases("alignas", "alignof", "asm", "auto", "bool", true)
      .cases("break", "case", "catch", "char", "char8_t", true)
      .cases("char16_t", "char32_t", "class", "concept", "const", true)
      .cases("consteval", "constexpr", "constinit", "const_cast", "continue", true)
      .cases("co_await", "co_return", "co_yield", "decltype", "default", true)
      .cases("delete", "do", "double", "dynamic_cast", "else", true)
      .cases("enum", "explicit", "export", "extern", "false", true)
      .cases("float", "for", "friend", "goto", "if", true)
      .cases("inline", "int", "long", "mutable", "namespace", true)
      .cases("new", "noexcept", "nullptr", "operator", "private", true)
      .cases("protected", "public", "register", "reinterpret_cast", "requires", true)
      .cases("return", "short", "signed", "sizeof", "static", true)
      .cases("static_assert", "static_cast", "struct", "switch", "template", true)
      .cases("this", "thread_local", "throw", "true", "try", true)
      .cases("typedef", "typeid", "typename", "union", "unsigned", true)
      .cases("using", "virtual", "void", "volatile", "wchar_t", true)
      .cases("while", true)
      .cases("and", "and_eq", "bitand", "bitor", "compl", true)
      .cases("not", "not_eq", "or", "or_eq", "xor", true)
      .cases("xor_eq", true)
      .cases("__FILE__", "__LINE__", "__DATE__", "__TIME__", "__func__", true)
      .cases("__STDC__", "__STDC_VERSION__", "__STDC_HOSTED__", "__cplusplus", true)
      .cases("__STDCPP_THREADS__", "__STDCPP_DEFAULT_NEW_ALIGNMENT__", true)
      .cases("__GNUC__", "__GNUC_MINOR__", "__GNUC_PATCHLEVEL__", true)
      .cases("__clang__", "__clang_major__", "__clang_minor__", "__clang_patchlevel__", true)
      .cases("__COUNTER__", "__INCLUDE_LEVEL__", "__BASE_FILE__", "__TIMESTAMP__", true)
      .cases("__MSC_VER", "__MSC_FULL_VER", "__MSVC_RUNTIME_CHECKS", true)
      .cases("__FUNCSIG__", "__FUNCDNAME__", "__cdecl", "__stdcall", "__fastcall", true)
      .cases("__forceinline", "__declspec", "__int8", "__int16", true)
      .cases("__int32", "__int64", "__ptr32", "__ptr64", true)
      .or_default(false);
}

std::string to_cxx_ident(std::string_view str) {
  std::string result;
  if (str.empty()) [[unlikely]] {
    result += '_';
    return result;  // nrvo
  }
  result.reserve(str.size());
  if (is_digit(str[0]))
    result += '_';
  for (char c : str) {
    if (is_utf8_multibyte(c)) {
      // все байты одного символа (и лид, и продолжения) >= 0x80,
      result += c;
    } else if (is_ident_continue_no_z(c)) {
      result += c;
    } else if (auto esc = escape_char(c); !esc.empty()) {
      result += esc;
    } else {
      result += escape_char_fallback(c);
    }
  }
  if (is_cxx_keyword(result))
    result += '_';
  return result;
}

std::string to_cxx_ident_simple(std::string_view str) {
  std::string result;
  if (str.empty()) [[unlikely]] {
    result += '_';
    return result;  // nrvo
  }
  result.reserve(str.size());
  if (is_digit(str[0]))
    result += '_';
  for (char c : str) {
    if (is_utf8_multibyte(c))
      result += c;
    else if (is_ident_continue(c))
      result += c;
    else
      result += '_';
  }
  if (is_cxx_keyword(result))
    result += '_';
  return result;
}

}  // namespace svarg::cxx
