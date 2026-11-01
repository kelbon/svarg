#pragma once

#include <string_view>

namespace svarg {

// string. Выставляется для операции с типом авторизации http (Bearer)
constexpr inline std::string_view x_bearer_format = "x-bearerFormat";
// oauth_flows_object. Выставляется для операции с типом авторизации oauth2
constexpr inline std::string_view x_auth_flows = "x-authFlows";

// oauth и openid разделены, чтобы не мета ключи не конфликтовали, если установлены оба. Если один тип
// установлен дважды, то... Сами виноваты

// vector<string>. Выставляется для операции с типом авторизации "oauth2"
constexpr inline std::string_view x_auth_scopes_oauth2 = "x-authScopesOauth2";
// vector<string>. Выставляется для операции с типом авторизации "openIdConnect"
constexpr inline std::string_view x_auth_scopes_openIdConnect = "x-authScopesOpenIdConnect";
// string. Выставляется для операции с типом авторизации "openIdConnect"
constexpr inline std::string_view x_openid_connect_url = "x-openIdConnectUrl";

// добавляет нормальное имя инлайн структуре вместо сгенерированного. Значение строка
// игнорируется у именованных структур
constexpr inline std::string_view x_name_hint = "x-name-hint";

// передаётся из поля format схемы в meta IR схемы. Значение строка
constexpr inline std::string_view x_format_hint = "x-format-hint";

// meta параметра операции, только если указаны явно: как массив/объект записывается в строку (OpenAPI
// style и explode). Значения строка и bool
constexpr inline std::string_view x_param_style = "x-style";
constexpr inline std::string_view x_param_explode = "x-explode";

}  // namespace svarg
