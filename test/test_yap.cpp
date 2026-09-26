#include <moko3/moko3.hpp>

#include "svarg/yap/from_binary.hpp"
#include "svarg/yap/to_binary.hpp"

struct test_inner {
  yap::uint_t x = 5;
  metel::box<test_inner> next;

  bool operator==(const test_inner&) const = default;

  YAP_OBJECT;
};

struct test_struct {
  test_inner field;
  bool b = false;
  std::vector<std::string> strings;
  std::vector<std::vector<test_struct>> inners;

  bool operator==(const test_struct&) const = default;

  YAP_OBJECT;
};

template <typename T, typename ParseAs = T>
[[nodiscard]] inline bool check_good(const T& x) {
  yap::binary_t bytes;
  yap::to_binary(x, bytes);
  ParseAs v;
  std::span unparsed = yap::from_binary(bytes, v);
  return unparsed.empty() && v == x;
}

TEST("to/from binary") {
  // --- фундаментальные типы ---
  REQUIRE(check_good(yap::uint_t{0}));
  REQUIRE(check_good(yap::uint_t{1}));
  REQUIRE(check_good(yap::uint_t{63000}));
  REQUIRE(check_good(yap::uint_t{1'000'000'000}));

  REQUIRE(check_good(yap::int_t{0}));
  REQUIRE(check_good(yap::int_t{-1}));
  REQUIRE(check_good(yap::int_t{123456}));
  REQUIRE(check_good(yap::int_t{-123456}));

  REQUIRE(check_good(true));
  REQUIRE(check_good(false));

  REQUIRE(check_good(std::string{}));
  REQUIRE(check_good(std::string{"hello, world!"}));
  REQUIRE(check_good(std::string(1000, 'x')));

  REQUIRE(check_good(std::vector<yap::int_t>{}));
  REQUIRE(check_good(std::vector<yap::int_t>{1, 2, 3, 4, 5}));
  REQUIRE(check_good(std::vector<std::string>{}));
  REQUIRE(check_good(std::vector<std::string>{"a", "bb", "ccc"}));
  REQUIRE(check_good(std::vector<std::vector<yap::int_t>>{{}}));
  REQUIRE(check_good(std::vector<std::vector<yap::int_t>>{{1, 2}, {}, {3}}));

  // --- test_inner: без рекурсии ---
  REQUIRE(check_good(test_inner{}));

  {
    test_inner inner;
    inner.x = 123;
    REQUIRE(check_good(inner));
  }

  // --- test_inner: один уровень вложенности через box ---
  {
    test_inner inner;
    inner.x = 1;
    inner.next = metel::box<test_inner>(test_inner{.x = 2});
    REQUIRE(check_good(inner));
  }

  // --- test_inner: несколько уровней вложенности ---
  {
    test_inner leaf{.x = 3};
    test_inner mid{
        .x = 2,
        .next = metel::box<test_inner>(leaf),
    };
    test_inner root{
        .x = 1,
        .next = metel::box<test_inner>(mid),
    };
    REQUIRE(check_good(root));
  }

  // --- test_struct: дефолтный ---
  REQUIRE(check_good(test_struct{}));

  // --- test_struct: простые поля заполнены ---
  {
    test_struct s;
    s.field.x = 10;
    s.b = true;
    s.strings = {"foo", "bar", "baz"};
    REQUIRE(check_good(s));
  }

  // --- test_struct: пустые контейнеры явно ---
  {
    test_struct s;
    s.strings = {};
    s.inners = {};
    REQUIRE(check_good(s));
  }

  // --- test_struct: вложенные test_struct в inners ---
  {
    test_struct a;
    a.field.x = 1;
    a.b = false;
    a.strings = {"a"};

    test_struct b;
    b.field.x = 2;
    b.b = true;
    b.strings = {"b1", "b2"};

    test_struct c;
    c.field.x = 3;

    test_struct s;
    s.field.x = 100;
    s.b = true;
    s.strings = {"top-level"};
    s.inners = {{a, b}, {}, {c}};

    REQUIRE(check_good(s));
  }

  // --- test_struct: field с рекурсией box + вложенные структуры ---
  {
    test_inner inner{
        .x = 42,
        .next = metel::box<test_inner>(test_inner{.x = 43}),
    };

    test_struct child;
    child.field.x = 7;
    child.strings = {"child"};

    test_struct s;
    s.field = inner;
    s.b = true;
    s.strings = {"deep", "nested", "test"};
    s.inners = {{child}};

    REQUIRE(check_good(s));
  }

  // --- вектор test_struct, включая дефолтные и заполненные элементы ---
  {
    std::vector<test_struct> vec;
    vec.push_back(test_struct{});

    test_struct s1;
    s1.b = true;
    s1.strings = {"one"};
    vec.push_back(s1);

    REQUIRE(check_good(vec));
  }
}

// эмулирует ситуацию, когда в новой версии удалили поле
struct test_struct_v2 {
  test_inner field;
  bool b = false;
  //  std::vector<std::string> strings;
  std::vector<std::vector<test_struct>> inners;

  bool operator==(const test_struct& t) const {
    return field == t.field && b == t.b && inners == t.inners;
  }

  YAP_OBJECT;
};

// эмулирует ситуацию, когда добавили новое опциональное поле
struct test_struct_v3 {
  test_inner field;
  bool b = false;
  std::vector<std::string> strings;
  std::vector<std::vector<test_struct>> inners;
  std::optional<yap::uint_t> extra;  // добавлено

  bool operator==(const test_struct& t) const {
    return field == t.field && b == t.b && strings == t.strings && inners == t.inners;
  }

  YAP_OBJECT;
};

// эмулирует ситуацию, когда обязательное поле сделали опциональным
struct test_struct_v4 {
  test_inner field;
  std::optional<bool> b;  // сделано опциональным
  std::vector<std::string> strings;
  std::vector<std::vector<test_struct>> inners;

  bool operator==(const test_struct& t) const {
    return field == t.field && b == t.b && strings == t.strings && inners == t.inners;
  }

  YAP_OBJECT;
};

TEST("backward compatibility") {
  // --- было поле - удалили (test_struct -> test_struct_v2) ---
  {
    test_struct s;
    s.field.x = 42;
    s.b = true;
    s.strings = {"a", "b", "c"};
    s.inners = {{test_struct{.field = {.x = 1}, .b = true}}, {}};

    REQUIRE((check_good<test_struct, test_struct_v2>(s)));
  }
  REQUIRE((check_good<test_struct, test_struct_v2>(test_struct{})));

  // --- не было поля - добавили (test_struct -> test_struct_v3, extra опционален) ---
  {
    test_struct s;
    s.field.x = 100;
    s.b = true;
    s.strings = {"top"};
    s.inners = {{test_struct{.field = {.x = 7}, .strings = {"nested"}}}};

    // старые данные -> новый код: extra должен получиться nullopt
    REQUIRE((check_good<test_struct, test_struct_v3>(s)));
  }
  {
    // новый код пишет extra со значением -> старый код должен просто его проигнорировать
    test_struct_v3 s;
    s.field.x = 5;
    s.b = false;
    s.strings = {"x", "y"};
    s.extra = yap::uint_t{999};

    REQUIRE((check_good<test_struct_v3, test_struct>(s)));
  }
  {
    // и extra = nullopt у новой версии -> старый код тоже должен отработать
    test_struct_v3 s;
    s.field.x = 6;
    s.extra = std::nullopt;

    REQUIRE((check_good<test_struct_v3, test_struct>(s)));
  }

  // --- было обязательное поле - сделали опциональным (test_struct.b -> test_struct_v4.b) ---
  {
    test_struct s;
    s.field.x = 11;
    s.b = true;
    s.strings = {"req"};

    // старые данные (b всегда есть) -> новый код: должно превратиться в has_value() == true
    REQUIRE((check_good<test_struct, test_struct_v4>(s)));
  }
  {
    // новый код пишет b с явным значением -> старый код должен прочитать то же значение
    test_struct_v4 s;
    s.field.x = 12;
    s.b = true;
    s.strings = {"opt"};

    REQUIRE((check_good<test_struct_v4, test_struct>(s)));
  }

  // --- числа распарсены по-другому: int/double/bool ---
  REQUIRE((check_good<yap::int_t, double>(yap::int_t{0})));
  REQUIRE((check_good<yap::int_t, double>(yap::int_t{42})));
  REQUIRE((check_good<yap::int_t, double>(yap::int_t{-1000})));

  REQUIRE((check_good<yap::int_t, yap::uint_t>(yap::int_t{0})));
  REQUIRE((check_good<yap::int_t, yap::uint_t>(yap::int_t{42})));
  // несовместимо REQUIRE((check_good<yap::int_t, yap::uint_t>(yap::int_t{-1000})));

  REQUIRE((check_good<yap::uint_t, yap::int_t>(yap::int_t{0})));
  REQUIRE((check_good<yap::uint_t, yap::int_t>(yap::int_t{42})));

  // так нельзя
  // REQUIRE((check_good<double, yap::int_t>(double{0})));
  // REQUIRE((check_good<double, yap::int_t>(double{42})));
  // REQUIRE((check_good<double, yap::int_t>(double{-1000})));

  // REQUIRE((check_good<yap::int_t, bool>(yap::int_t{0})));
  // REQUIRE((check_good<yap::int_t, bool>(yap::int_t{1})));

  // REQUIRE((check_good<bool, yap::int_t>(false)));
  // REQUIRE((check_good<bool, yap::int_t>(true)));
}

struct empty_obj {
  bool operator==(const empty_obj&) const = default;

  YAP_OBJECT;
};
struct maybe_empty_obj {
  std::optional<yap::int_t> i;

  bool operator==(const maybe_empty_obj&) const = default;

  YAP_OBJECT;
};

TEST("empty parsing") {
  REQUIRE(check_good(empty_obj{}));
  REQUIRE(check_good(maybe_empty_obj{}));
}

struct Location {
  YAP_OBJECT;
  YAP_INDEXED_KEYS;
  double lat;
  double lon;

  bool operator==(const Location&) const = default;
};

struct Calibration {
  YAP_OBJECT;
  YAP_INDEXED_KEYS;

  double offset;
  yap::int_t factor;

  bool operator==(const Calibration&) const = default;
};

struct Sensor {
  YAP_OBJECT;
  YAP_INDEXED_KEYS;

  yap::uint_t id;
  std::string name;
  bool active;
  std::vector<double> readings;
  Location location;
  std::vector<yap::huffman_encoded_string> tags;
  std::vector<Calibration> calibrations;

  bool operator==(const Sensor&) const = default;
};

struct Sensor_v2 {
  YAP_OBJECT;
  YAP_INDEXED_KEYS;

  yap::uint_t id;
  std::string name;
  bool active;
  std::vector<double> readings;
  Location location;
  std::vector<yap::huffman_encoded_string> tags;
  yap::skipped_field<int> fdsfdsf;
  // удалено последнее поле calibrations

  bool operator==(const Sensor& other) const {
    return id == other.id && name == other.name && readings == other.readings && location == other.location &&
           tags == other.tags;
  }
};

constexpr std::string_view json_equivalent =
    R"({"id":42,"name":"sensor","active":true,"readings":[1.5,2.75,-0.3],"location":{"lat":52.37,"lon":4.895},"tags":["temp","outdoor"],"calibrations":[{"offset":0.1,"factor":2},{"offset":-0.05,"factor":-3}]})";

TEST("compare with json") {
  Sensor sensor = {
      .id = 42,
      .name = "sensor",
      .active = true,
      .readings = {1.5, 2.75, -0.3},
      .location = {.lat = 52.37, .lon = 4.895},
      .tags = {"temp", "outdoor"},
      .calibrations =
          {
              {.offset = 0.1, .factor = 2},
              {.offset = -0.05, .factor = -3},
          },
  };

  yap::binary_t bytes = yap::to_binary(sensor);
  static_assert(json_equivalent.size() == 201);
  REQUIRE(bytes.size() == 83);  // 158 с именами полей
  Sensor res;
  yap::from_binary(bytes, res);
  REQUIRE(res == sensor);
  // декодирование когда в байтах индекс больше, чем полей есть в типе (последнее поле удалено)
  Sensor_v2 res2;
  yap::from_binary(bytes, res2);
  REQUIRE(res2 == sensor);
}

struct longstr {
  yap::huffman_encoded_string longstring;
  YAP_OBJECT;
  YAP_INDEXED_KEYS;
};

TEST("huffman strings") {
  longstr l;
  l.longstring = json_equivalent;
  auto bytes = yap::to_binary(l);
  REQUIRE(bytes.size() == 183);
  longstr res;
  yap::from_binary(bytes, res);
  REQUIRE(res.longstring == l.longstring);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
