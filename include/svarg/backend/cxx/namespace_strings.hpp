#pragma once

#include "svarg/utils/json_pointer.hpp"

namespace svarg::cxx {

// превращает jp в неймспейс валидный в С++
std::string jsonptr2namespace(const json_pointer& jp);

// инпуты должны быть вида a::b::c, неймспейсами начиная с корневого
//
// возвращает строку максимально короткого неймспейса для обращения из namespace a в namespace b
// при этом исключая любые возможные конфликты имён
// A="a::b::c", B="b::c",    => "b::c"
// A="b::c",    B="d::e",    => "::d::e"
// A="a::b::c", B="a::b",    => "::a::b"
// A="a::b::c", B="b",       => "::b"
// A="a",       B="a::b::c", => "b::c"
std::string namespace_relative(std::string_view A, std::string_view B);

std::string namespace_relative(const json_pointer& A, const json_pointer& B);

}  // namespace svarg::cxx
