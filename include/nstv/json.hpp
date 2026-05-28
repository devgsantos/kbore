#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace nstv {

class Json {
public:
  using object_t = std::map<std::string, Json>;
  using array_t = std::vector<Json>;
  using value_t = std::variant<std::nullptr_t, bool, double, std::string, object_t, array_t>;

  Json() : value_(nullptr) {}
  Json(std::nullptr_t) : value_(nullptr) {}
  Json(bool v) : value_(v) {}
  Json(double v) : value_(v) {}
  Json(int v) : value_(static_cast<double>(v)) {}
  Json(std::string v) : value_(std::move(v)) {}
  Json(const char *v) : value_(std::string(v)) {}
  Json(object_t v) : value_(std::move(v)) {}
  Json(array_t v) : value_(std::move(v)) {}

  bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
  bool isBool() const { return std::holds_alternative<bool>(value_); }
  bool isNumber() const { return std::holds_alternative<double>(value_); }
  bool isString() const { return std::holds_alternative<std::string>(value_); }
  bool isObject() const { return std::holds_alternative<object_t>(value_); }
  bool isArray() const { return std::holds_alternative<array_t>(value_); }

  bool asBool(bool fallback = false) const { return isBool() ? std::get<bool>(value_) : fallback; }
  double asNumber(double fallback = 0) const { return isNumber() ? std::get<double>(value_) : fallback; }
  int asInt(int fallback = 0) const { return isNumber() ? static_cast<int>(std::get<double>(value_)) : fallback; }
  const std::string &asString() const { return std::get<std::string>(value_); }
  std::string asString(const std::string &fallback) const { return isString() ? std::get<std::string>(value_) : fallback; }

  const object_t &asObject() const { return std::get<object_t>(value_); }
  object_t &asObject() { return std::get<object_t>(value_); }
  const array_t &asArray() const { return std::get<array_t>(value_); }
  array_t &asArray() { return std::get<array_t>(value_); }

  const Json &operator[](const std::string &key) const {
    static const Json nullValue;
    if (!isObject()) return nullValue;
    const auto &obj = std::get<object_t>(value_);
    auto it = obj.find(key);
    return it == obj.end() ? nullValue : it->second;
  }

  Json &operator[](const std::string &key) {
    if (!isObject()) value_ = object_t{};
    return std::get<object_t>(value_)[key];
  }

  bool contains(const std::string &key) const {
    if (!isObject()) return false;
    const auto &obj = std::get<object_t>(value_);
    return obj.find(key) != obj.end();
  }

  static Json parse(const std::string &text);
  std::string stringify() const;

private:
  value_t value_;
};

std::string jsonEscape(const std::string &value);

class JsonParser {
public:
  explicit JsonParser(const std::string &text) : text_(text) {}
  Json parse();

private:
  Json parseValue();
  Json parseObject();
  Json parseArray();
  Json parseString();
  Json parseNumber();
  Json parseLiteral(const std::string &literal, Json value);
  void skipWhitespace();
  char peek() const;
  char consume();
  void expect(char c);

  const std::string &text_;
  std::size_t pos_ = 0;
};

} // namespace nstv
