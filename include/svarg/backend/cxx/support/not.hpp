#pragma once

namespace svarg::cxx_support {

// по сути просто тег для парсинга. Сериализация not невозможна,
// т.к. этот тип не может быть представлением, только требованием
template <typename T>
struct not_a {};

}  // namespace svarg::cxx_support
