#include "nstv/manifest_json.hpp"
#include "nstv/json.hpp"
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace nstv {
namespace {

static void yieldParserPeriodically() {
  static thread_local std::size_t steps = 0;

  if (++steps % 512 == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

class FastJsonReader {
public:
  explicit FastJsonReader(const std::string &text) : text_(text) {}

  void skipWs() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }

  char peek() {
    skipWs();
    return pos_ < text_.size() ? text_[pos_] : '\0';
  }

  char rawPeek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
  }

  char get() {
    if (pos_ >= text_.size()) {
      throw std::runtime_error("Unexpected end of JSON");
    }
    return text_[pos_++];
  }

  void expect(char expected) {
    skipWs();
    char got = get();
    if (got != expected) {
      throw std::runtime_error(std::string("Expected '") + expected + "'");
    }
  }

  bool consumeIf(char expected) {
    skipWs();
    if (rawPeek() != expected) {
      return false;
    }
    ++pos_;
    return true;
  }

  std::string parseString() {
    skipWs();
    if (get() != '"') {
      throw std::runtime_error("Expected JSON string");
    }

    std::string out;
    out.reserve(32);

    while (pos_ < text_.size()) {
      char c = get();

      if (c == '"') {
        return out;
      }

      if (c != '\\') {
        out.push_back(c);
        continue;
      }

      char e = get();
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            throw std::runtime_error("Invalid unicode escape");
          }

          std::string hex = text_.substr(pos_, 4);
          pos_ += 4;
          int code = std::strtol(hex.c_str(), nullptr, 16);

          if (code >= 0 && code <= 0x7f) {
            out.push_back(static_cast<char>(code));
          } else {
            out.push_back('?');
          }
          break;
        }
        default:
          throw std::runtime_error("Invalid string escape");
      }
    }

    throw std::runtime_error("Unterminated JSON string");
  }

  bool parseBool(bool fallback = false) {
    skipWs();

    if (text_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      return true;
    }

    if (text_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      return false;
    }

    skipValue();
    return fallback;
  }

  int parseInt(int fallback = 0) {
    skipWs();

    if (rawPeek() == 'n') {
      skipValue();
      return fallback;
    }

    char *end = nullptr;
    const char *start = text_.c_str() + pos_;
    long value = std::strtol(start, &end, 10);

    if (end == start) {
      skipValue();
      return fallback;
    }

    pos_ = static_cast<std::size_t>(end - text_.c_str());
    return static_cast<int>(value);
  }

  void skipValue() {
    skipWs();
    char c = rawPeek();

    if (c == '{') {
      skipObject();
      return;
    }

    if (c == '[') {
      skipArray();
      return;
    }

    if (c == '"') {
      parseString();
      return;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
      parseInt(0);
      while (pos_ < text_.size()) {
        char n = text_[pos_];
        if (!(std::isdigit(static_cast<unsigned char>(n)) || n == '.' || n == 'e' || n == 'E' || n == '+' || n == '-')) {
          break;
        }
        ++pos_;
      }
      return;
    }

    if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return; }
    if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return; }
    if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; return; }

    throw std::runtime_error("Invalid JSON value");
  }

private:
  void skipObject() {
    expect('{');

    if (consumeIf('}')) {
      return;
    }

    while (true) {
      yieldParserPeriodically();
      parseString();
      expect(':');
      skipValue();

      if (consumeIf('}')) {
        break;
      }

      expect(',');
    }
  }

  void skipArray() {
    expect('[');

    if (consumeIf(']')) {
      return;
    }

    while (true) {
      yieldParserPeriodically();
      skipValue();

      if (consumeIf(']')) {
        break;
      }

      expect(',');
    }
  }

  const std::string &text_;
  std::size_t pos_ = 0;
};

static Manifest parseManifestObject(FastJsonReader &reader, Provider fallbackProvider, const std::string &fallbackSource);
static MediaNode parseNodeObject(FastJsonReader &reader, const std::string &fallbackType);
static TypeGroup parseTypeObject(FastJsonReader &reader);

static void appendNodeTypes(const MediaNode &node, Manifest &manifest) {
  if (node.type.empty()) {
    return;
  }

  TypeGroup group;
  group.id = streamTypeFromString(node.type);
  group.label = node.title.empty() ? toString(group.id) : node.title;
  group.totalChannels = node.totalChannels > 0 ? node.totalChannels : node.totalItems;

  for (const auto &child : node.children) {
    yieldParserPeriodically();

    Category category;
    category.id = child.id;
    category.name = child.title.empty() ? child.name : child.title;
    category.totalChannels = child.totalChannels > 0 ? child.totalChannels : child.totalItems;
    category.type = streamTypeFromString(child.type.empty() ? node.type : child.type);
    group.categories.push_back(category);
  }

  for (const auto &existing : manifest.types) {
    if (existing.id == group.id) {
      return;
    }
  }

  manifest.types.push_back(std::move(group));
}

static Category parseCategoryObject(FastJsonReader &reader, StreamType fallbackType) {
  Category category;
  category.type = fallbackType;

  reader.expect('{');

  if (reader.consumeIf('}')) {
    return category;
  }

  while (true) {
    std::string key = reader.parseString();
    reader.expect(':');

    if (key == "id") {
      category.id = reader.parseString();
    } else if (key == "name" || key == "title") {
      category.name = reader.parseString();
    } else if (key == "totalChannels" || key == "totalItems" || key == "count") {
      category.totalChannels = reader.parseInt(category.totalChannels);
    } else if (key == "type" || key == "streamType") {
      category.type = streamTypeFromString(reader.parseString());
    } else {
      reader.skipValue();
    }

    if (reader.consumeIf('}')) {
      break;
    }

    reader.expect(',');
  }

  if (category.name.empty()) {
    category.name = category.id.empty() ? "Uncategorized" : category.id;
  }

  return category;
}

static TypeGroup parseTypeObject(FastJsonReader &reader) {
  TypeGroup group;
  group.id = StreamType::Live;

  reader.expect('{');

  if (reader.consumeIf('}')) {
    group.label = toString(group.id);
    return group;
  }

  while (true) {
    std::string key = reader.parseString();
    reader.expect(':');

    if (key == "id" || key == "type") {
      group.id = streamTypeFromString(reader.parseString());
    } else if (key == "label" || key == "name" || key == "title") {
      group.label = reader.parseString();
    } else if (key == "totalChannels" || key == "totalItems" || key == "count") {
      group.totalChannels = reader.parseInt(group.totalChannels);
    } else if (key == "categories" || key == "children") {
      reader.expect('[');

      if (!reader.consumeIf(']')) {
        while (true) {
          yieldParserPeriodically();

          if (reader.peek() == '{') {
            group.categories.push_back(parseCategoryObject(reader, group.id));
          } else {
            reader.skipValue();
          }

          if (reader.consumeIf(']')) {
            break;
          }

          reader.expect(',');
        }
      }
    } else {
      reader.skipValue();
    }

    if (reader.consumeIf('}')) {
      break;
    }

    reader.expect(',');
  }

  if (group.label.empty()) {
    group.label = toString(group.id);
  }

  return group;
}

static MediaNode parseNodeObject(FastJsonReader &reader, const std::string &fallbackType) {
  yieldParserPeriodically();

  MediaNode node;
  node.type = fallbackType;

  reader.expect('{');

  if (reader.consumeIf('}')) {
    return node;
  }

  while (true) {
    std::string key = reader.parseString();
    reader.expect(':');

    if (key == "id") {
      node.id = reader.parseString();
    } else if (key == "title") {
      node.title = reader.parseString();
    } else if (key == "name") {
      node.name = reader.parseString();
    } else if (key == "type" || key == "streamType") {
      node.type = reader.parseString();
    } else if (key == "kind") {
      node.kind = reader.parseString();
    } else if (key == "url" || key == "playbackUrl") {
      node.url = reader.parseString();
    } else if (key == "logo" || key == "stream_icon" || key == "cover") {
      node.logo = reader.parseString();
    } else if (key == "group" || key == "category") {
      node.group = reader.parseString();
    } else if (key == "groupId" || key == "categoryId" || key == "category_id") {
      node.groupId = reader.parseString();
    } else if (key == "totalItems" || key == "totalChannels" || key == "count") {
      int value = reader.parseInt(0);
      if (key == "totalChannels") {
        node.totalChannels = value;
        if (node.totalItems <= 0) node.totalItems = value;
      } else {
        node.totalItems = value;
        if (node.totalChannels <= 0) node.totalChannels = value;
      }
    } else if (key == "childCount" || key == "childrenCount") {
      node.childCount = reader.parseInt(node.childCount);
    } else if (key == "hasChildren") {
      node.hasChildren = reader.parseBool(node.hasChildren);
    } else if (key == "playable") {
      node.playable = reader.parseBool(node.playable);
    } else if (key == "children") {
      reader.expect('[');

      if (!reader.consumeIf(']')) {
        while (true) {
          yieldParserPeriodically();

          if (reader.peek() == '{') {
            node.children.push_back(parseNodeObject(reader, node.type));
          } else {
            reader.skipValue();
          }

          if (reader.consumeIf(']')) {
            break;
          }

          reader.expect(',');
        }
      }
    } else {
      reader.skipValue();
    }

    if (reader.consumeIf('}')) {
      break;
    }

    reader.expect(',');
  }

  if (node.type.empty()) {
    node.type = "live";
  }

  if (node.title.empty()) {
    node.title = node.name.empty() ? (node.id.empty() ? "Untitled" : node.id) : node.name;
  }

  if (node.name.empty()) {
    node.name = node.title;
  }

  if (node.playable == false && !node.url.empty()) {
    node.playable = true;
  }

  if (node.childCount <= 0 && !node.children.empty()) {
    node.childCount = static_cast<int>(node.children.size());
  }

  if (!node.hasChildren && (!node.children.empty() || node.childCount > 0)) {
    node.hasChildren = true;
  }

  if (node.kind.empty()) {
    node.kind = !node.hasChildren && node.children.empty() && node.playable ? "item" : "folder";
  }

  if (node.id.empty()) {
    node.id = node.type + ":" + node.title;
  }

  return node;
}

static void parseNodesArray(FastJsonReader &reader, Manifest &manifest) {
  reader.expect('[');

  if (reader.consumeIf(']')) {
    return;
  }

  while (true) {
    yieldParserPeriodically();

    if (reader.peek() == '{') {
      MediaNode node = parseNodeObject(reader, "");

      if (!node.id.empty() || !node.title.empty() || !node.children.empty()) {
        appendNodeTypes(node, manifest);
        manifest.nodes.push_back(std::move(node));
      }
    } else {
      reader.skipValue();
    }

    if (reader.consumeIf(']')) {
      break;
    }

    reader.expect(',');
  }
}

static void parseTypesArray(FastJsonReader &reader, Manifest &manifest) {
  reader.expect('[');

  if (reader.consumeIf(']')) {
    return;
  }

  while (true) {
    yieldParserPeriodically();

    if (reader.peek() == '{') {
      TypeGroup group = parseTypeObject(reader);

      bool exists = false;
      for (const auto &existing : manifest.types) {
        if (existing.id == group.id) {
          exists = true;
          break;
        }
      }

      if (!exists) {
        manifest.types.push_back(std::move(group));
      }
    } else {
      reader.skipValue();
    }

    if (reader.consumeIf(']')) {
      break;
    }

    reader.expect(',');
  }
}

static Manifest parseManifestObject(FastJsonReader &reader, Provider fallbackProvider, const std::string &fallbackSource) {
  Manifest manifest;
  manifest.provider = fallbackProvider;
  manifest.source = fallbackSource;

  reader.expect('{');

  if (reader.consumeIf('}')) {
    return manifest;
  }

  while (true) {
    std::string key = reader.parseString();
    reader.expect(':');

    if (key == "ok") {
      reader.parseBool(true);
    } else if (key == "manifest" || key == "data") {
      if (reader.peek() == '{') {
        Manifest nested = parseManifestObject(reader, manifest.provider, manifest.source);

        if (!nested.id.empty()) manifest.id = std::move(nested.id);
        if (!nested.name.empty()) manifest.name = std::move(nested.name);
        if (!nested.source.empty()) manifest.source = std::move(nested.source);
        if (nested.provider != Provider::Local || fallbackProvider == Provider::Local) manifest.provider = nested.provider;
        if (nested.totalChannels > 0) manifest.totalChannels = nested.totalChannels;
        if (nested.totalItems > 0) manifest.totalItems = nested.totalItems;
        if (!nested.nodes.empty()) manifest.nodes = std::move(nested.nodes);
        if (!nested.types.empty()) manifest.types = std::move(nested.types);
      } else {
        reader.skipValue();
      }
    } else if (key == "id") {
      manifest.id = reader.parseString();
    } else if (key == "name" || key == "title") {
      manifest.name = reader.parseString();
    } else if (key == "source" || key == "url") {
      manifest.source = reader.parseString();
    } else if (key == "provider") {
      manifest.provider = providerFromString(reader.parseString());
    } else if (key == "totalChannels") {
      manifest.totalChannels = reader.parseInt(manifest.totalChannels);
      if (manifest.totalItems <= 0) manifest.totalItems = manifest.totalChannels;
    } else if (key == "totalItems") {
      manifest.totalItems = reader.parseInt(manifest.totalItems);
      if (manifest.totalChannels <= 0) manifest.totalChannels = manifest.totalItems;
    } else if (key == "nodes") {
      parseNodesArray(reader, manifest);
    } else if (key == "types") {
      parseTypesArray(reader, manifest);
    } else {
      reader.skipValue();
    }

    if (reader.consumeIf('}')) {
      break;
    }

    reader.expect(',');
  }

  if (manifest.provider == Provider::Local) {
    manifest.provider = fallbackProvider;
  }

  if (manifest.source.empty()) {
    manifest.source = fallbackSource;
  }

  if (manifest.name.empty()) {
    manifest.name = manifest.source.empty() ? "kboré Playlist" : manifest.source;
  }

  if (manifest.id.empty()) {
    manifest.id = toString(manifest.provider) + "-playlist";
  }

  if (manifest.totalChannels <= 0) {
    for (const auto &type : manifest.types) {
      manifest.totalChannels += type.totalChannels;
    }
  }

  if (manifest.totalItems <= 0) {
    manifest.totalItems = manifest.totalChannels;
  }

  return manifest;
}

static void writeEscaped(std::ostream &out, const std::string &value) {
  out << '"';

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
          out << "?";
        } else {
          out << ch;
        }
        break;
    }
  }

  out << '"';
}

static void writeNode(std::ostream &out, const MediaNode &node) {
  out << '{';
  out << "\"id\":"; writeEscaped(out, node.id);
  out << ",\"title\":"; writeEscaped(out, node.title);
  out << ",\"name\":"; writeEscaped(out, node.name);
  out << ",\"type\":"; writeEscaped(out, node.type);
  out << ",\"kind\":"; writeEscaped(out, node.kind);
  out << ",\"url\":"; writeEscaped(out, node.url);
  out << ",\"logo\":"; writeEscaped(out, node.logo);
  out << ",\"group\":"; writeEscaped(out, node.group);
  out << ",\"groupId\":"; writeEscaped(out, node.groupId);
  out << ",\"totalItems\":" << node.totalItems;
  out << ",\"totalChannels\":" << node.totalChannels;
  out << ",\"childCount\":" << node.childCount;
  out << ",\"hasChildren\":" << (node.hasChildren ? "true" : "false");
  out << ",\"playable\":" << (node.playable ? "true" : "false");
  out << ",\"children\":[";

  for (std::size_t i = 0; i < node.children.size(); ++i) {
    if (i) out << ',';
    writeNode(out, node.children[i]);
  }

  out << "]}";
}

static void writeType(std::ostream &out, const TypeGroup &type) {
  out << '{';
  out << "\"id\":"; writeEscaped(out, toString(type.id));
  out << ",\"label\":"; writeEscaped(out, type.label);
  out << ",\"totalChannels\":" << type.totalChannels;
  out << ",\"categories\":[";

  for (std::size_t i = 0; i < type.categories.size(); ++i) {
    const Category &category = type.categories[i];
    if (i) out << ',';
    out << '{';
    out << "\"id\":"; writeEscaped(out, category.id);
    out << ",\"name\":"; writeEscaped(out, category.name);
    out << ",\"totalChannels\":" << category.totalChannels;
    out << ",\"type\":"; writeEscaped(out, toString(category.type));
    out << '}';
  }

  out << "]}";
}

} // namespace

Manifest manifestFromJsonTextFast(
  const std::string &text,
  Provider fallbackProvider,
  const std::string &fallbackSource
) {
  FastJsonReader reader(text);
  Manifest manifest = parseManifestObject(reader, fallbackProvider, fallbackSource);
  reader.skipWs();
  return manifest;
}

void writeManifestJson(std::ostream &out, const Manifest &manifest) {
  out << '{';
  out << "\"id\":"; writeEscaped(out, manifest.id);
  out << ",\"name\":"; writeEscaped(out, manifest.name);
  out << ",\"provider\":"; writeEscaped(out, toString(manifest.provider));
  out << ",\"source\":"; writeEscaped(out, manifest.source);
  out << ",\"totalChannels\":" << manifest.totalChannels;
  out << ",\"totalItems\":" << manifest.totalItems;
  out << ",\"nodes\":[";

  for (std::size_t i = 0; i < manifest.nodes.size(); ++i) {
    if (i) out << ',';
    writeNode(out, manifest.nodes[i]);
  }

  out << "],\"types\":[";

  for (std::size_t i = 0; i < manifest.types.size(); ++i) {
    if (i) out << ',';
    writeType(out, manifest.types[i]);
  }

  out << "]}";
}

std::string manifestToJsonTextFast(const Manifest &manifest) {
  std::ostringstream out;
  writeManifestJson(out, manifest);
  return out.str();
}

} // namespace nstv
