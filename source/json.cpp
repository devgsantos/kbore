#include "nstv/json.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace nstv {

Json Json::parse(const std::string &text) {
  return JsonParser(text).parse();
}

std::string jsonEscape(const std::string &value) {
  std::ostringstream out;
  for (char ch : value) {
    switch (ch) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
        } else {
          out << ch;
        }
    }
  }
  return out.str();
}

std::string Json::stringify() const {
  if (isNull()) return "null";
  if (isBool()) return std::get<bool>(value_) ? "true" : "false";
  if (isNumber()) {
    std::ostringstream out;
    double v = std::get<double>(value_);
    if (std::floor(v) == v) out << static_cast<long long>(v);
    else out << v;
    return out.str();
  }
  if (isString()) return "\"" + jsonEscape(std::get<std::string>(value_)) + "\"";
  if (isArray()) {
    std::string out = "[";
    const auto &arr = std::get<array_t>(value_);
    for (std::size_t i = 0; i < arr.size(); ++i) {
      if (i) out += ",";
      out += arr[i].stringify();
    }
    out += "]";
    return out;
  }
  std::string out = "{";
  const auto &obj = std::get<object_t>(value_);
  bool first = true;
  for (const auto &[key, value] : obj) {
    if (!first) out += ",";
    first = false;
    out += "\"" + jsonEscape(key) + "\":" + value.stringify();
  }
  out += "}";
  return out;
}

Json JsonParser::parse() {
  skipWhitespace();
  Json value = parseValue();
  skipWhitespace();
  if (pos_ != text_.size()) throw std::runtime_error("Unexpected trailing JSON data");
  return value;
}

Json JsonParser::parseValue() {
  skipWhitespace();
  char c = peek();
  if (c == '{') return parseObject();
  if (c == '[') return parseArray();
  if (c == '"') return parseString();
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
  if (text_.compare(pos_, 4, "true") == 0) return parseLiteral("true", Json(true));
  if (text_.compare(pos_, 5, "false") == 0) return parseLiteral("false", Json(false));
  if (text_.compare(pos_, 4, "null") == 0) return parseLiteral("null", Json(nullptr));
  throw std::runtime_error("Invalid JSON value");
}

Json JsonParser::parseObject() {
  expect('{');
  Json::object_t obj;
  skipWhitespace();
  if (peek() == '}') { consume(); return Json(obj); }
  while (true) {
    skipWhitespace();
    std::string key = parseString().asString();
    skipWhitespace();
    expect(':');
    obj[key] = parseValue();
    skipWhitespace();
    char c = consume();
    if (c == '}') break;
    if (c != ',') throw std::runtime_error("Expected ',' or '}' in object");
  }
  return Json(obj);
}

Json JsonParser::parseArray() {
  expect('[');
  Json::array_t arr;
  skipWhitespace();
  if (peek() == ']') { consume(); return Json(arr); }
  while (true) {
    arr.push_back(parseValue());
    skipWhitespace();
    char c = consume();
    if (c == ']') break;
    if (c != ',') throw std::runtime_error("Expected ',' or ']' in array");
  }
  return Json(arr);
}

Json JsonParser::parseString() {
  expect('"');
  std::string out;
  while (pos_ < text_.size()) {
    char c = consume();
    if (c == '"') return Json(out);
    if (c == '\\') {
      char e = consume();
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          // Minimal UTF-8 support for ASCII unicode escapes. Non-ASCII becomes '?'.
          if (pos_ + 4 > text_.size()) throw std::runtime_error("Invalid unicode escape");
          std::string hex = text_.substr(pos_, 4);
          pos_ += 4;
          int code = std::strtol(hex.c_str(), nullptr, 16);
          out += (code >= 0 && code <= 0x7f) ? static_cast<char>(code) : '?';
          break;
        }
        default: throw std::runtime_error("Invalid string escape");
      }
    } else {
      out += c;
    }
  }
  throw std::runtime_error("Unterminated string");
}

Json JsonParser::parseNumber() {
  std::size_t start = pos_;
  if (peek() == '-') consume();
  while (std::isdigit(static_cast<unsigned char>(peek()))) consume();
  if (peek() == '.') {
    consume();
    while (std::isdigit(static_cast<unsigned char>(peek()))) consume();
  }
  if (peek() == 'e' || peek() == 'E') {
    consume();
    if (peek() == '+' || peek() == '-') consume();
    while (std::isdigit(static_cast<unsigned char>(peek()))) consume();
  }
  return Json(std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
}

Json JsonParser::parseLiteral(const std::string &literal, Json value) {
  if (text_.compare(pos_, literal.size(), literal) != 0) throw std::runtime_error("Invalid literal");
  pos_ += literal.size();
  return value;
}

void JsonParser::skipWhitespace() {
  while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
}

char JsonParser::peek() const {
  return pos_ < text_.size() ? text_[pos_] : '\0';
}

char JsonParser::consume() {
  if (pos_ >= text_.size()) throw std::runtime_error("Unexpected end of JSON");
  return text_[pos_++];
}

void JsonParser::expect(char c) {
  if (consume() != c) throw std::runtime_error(std::string("Expected '") + c + "'");
}

} // namespace nstv
