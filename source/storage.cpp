#include "nstv/storage.hpp"
#include "nstv/json.hpp"
#include <cctype>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace nstv {

std::string dataDir() {
#ifdef __SWITCH__
  return "sdmc:/switch/nstv";
#else
  return ".";
#endif
}

std::string activeManifestPath() {
  return dataDir() + "/active-manifest.json";
}

std::string cacheDir() {
  return dataDir() + "/cache";
}

static void ensureDir(const std::string &path) {
  mkdir(path.c_str(), 0777);
}

static void ensureDataDir() {
#ifdef __SWITCH__
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/nstv", 0777);
  mkdir("sdmc:/switch/nstv/cache", 0777);
#else
  mkdir(".", 0777);
  mkdir("./cache", 0777);
#endif
}

static std::string safeFilePart(const std::string &value) {
  std::string output;

  for (char ch : value) {
    unsigned char c = static_cast<unsigned char>(ch);

    if (std::isalnum(c)) {
      output.push_back(static_cast<char>(std::tolower(c)));
      continue;
    }

    if (ch == '-' || ch == '_' || ch == '.') {
      output.push_back(ch);
      continue;
    }

    output.push_back('_');
  }

  if (output.empty()) return "item";
  return output;
}

std::string channelPageCachePath(
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int page
) {
  return cacheDir() + "/" +
    safeFilePart(toString(provider)) + "_" +
    safeFilePart(toString(type)) + "_" +
    safeFilePart(categoryId) + "_page_" +
    std::to_string(page) + ".json";
}

static Json manifestToJson(const Manifest &manifest) {
  Json root(Json::object_t{});
  root["id"] = manifest.id;
  root["name"] = manifest.name;
  root["source"] = manifest.source;
  root["provider"] = toString(manifest.provider);
  root["totalChannels"] = manifest.totalChannels;

  Json::array_t types;
  for (const auto &type : manifest.types) {
    Json typeJson(Json::object_t{});
    typeJson["id"] = toString(type.id);
    typeJson["label"] = type.label;
    typeJson["totalChannels"] = type.totalChannels;

    Json::array_t categories;
    for (const auto &category : type.categories) {
      Json cat(Json::object_t{});
      cat["id"] = category.id;
      cat["name"] = category.name;
      cat["totalChannels"] = category.totalChannels;
      cat["type"] = toString(category.type);
      categories.push_back(cat);
    }

    typeJson["categories"] = Json(categories);
    types.push_back(typeJson);
  }

  root["types"] = Json(types);
  return root;
}

static Manifest manifestFromJson(const Json &json) {
  Manifest manifest;
  manifest.id = json["id"].asString("cached-playlist");
  manifest.name = json["name"].asString("Cached Playlist");
  manifest.source = json["source"].asString("");
  manifest.provider = providerFromString(json["provider"].asString("local"));
  manifest.totalChannels = json["totalChannels"].asInt(0);

  if (json["types"].isArray()) {
    for (const auto &typeJson : json["types"].asArray()) {
      TypeGroup type;
      type.id = streamTypeFromString(typeJson["id"].asString("live"));
      type.label = typeJson["label"].asString(toString(type.id));
      type.totalChannels = typeJson["totalChannels"].asInt(0);

      if (typeJson["categories"].isArray()) {
        for (const auto &catJson : typeJson["categories"].asArray()) {
          Category cat;
          cat.id = catJson["id"].asString("");
          cat.name = catJson["name"].asString("Uncategorized");
          cat.totalChannels = catJson["totalChannels"].asInt(0);
          cat.type = streamTypeFromString(catJson["type"].asString(toString(type.id)));
          type.categories.push_back(cat);
        }
      }

      manifest.types.push_back(type);
    }
  }

  return manifest;
}

static Json channelToJson(const Channel &channel) {
  Json json(Json::object_t{});
  json["id"] = channel.id;
  json["name"] = channel.name;
  json["url"] = channel.url;
  json["logo"] = channel.logo;
  json["group"] = channel.group;
  json["groupId"] = channel.groupId;
  json["type"] = toString(channel.type);
  return json;
}

static Channel channelFromJson(const Json &json) {
  Channel channel;
  channel.id = json["id"].asString("");
  channel.name = json["name"].asString("Channel");
  channel.url = json["url"].asString("");
  channel.logo = json["logo"].asString("");
  channel.group = json["group"].asString("");
  channel.groupId = json["groupId"].asString("");
  channel.type = streamTypeFromString(json["type"].asString("live"));
  return channel;
}

static Json channelPageToJson(const ChannelPage &page) {
  Json root(Json::object_t{});
  root["page"] = page.page;
  root["pageSize"] = page.pageSize;
  root["totalChannels"] = page.totalChannels;
  root["totalPages"] = page.totalPages;
  root["hasNextPage"] = page.hasNextPage;

  Json::array_t channels;
  for (const auto &channel : page.channels) {
    channels.push_back(channelToJson(channel));
  }
  root["channels"] = Json(channels);

  return root;
}

static ChannelPage channelPageFromJson(const Json &json) {
  ChannelPage page;
  page.page = json["page"].asInt(1);
  page.pageSize = json["pageSize"].asInt(20);
  page.totalChannels = json["totalChannels"].asInt(json["total"].asInt(0));
  page.totalPages = json["totalPages"].asInt(1);
  page.hasNextPage = json["hasNextPage"].asBool(page.page < page.totalPages);

  const Json &channelsJson = json["channels"].isArray() ? json["channels"] : json["items"];

  if (channelsJson.isArray()) {
    for (const auto &item : channelsJson.asArray()) {
      page.channels.push_back(channelFromJson(item));
    }
  }

  return page;
}

bool saveManifest(const Manifest &manifest) {
  ensureDataDir();

  std::ofstream file(activeManifestPath(), std::ios::binary);
  if (!file) return false;

  file << manifestToJson(manifest).stringify();
  return true;
}

bool loadManifest(Manifest &manifest) {
  std::ifstream file(activeManifestPath(), std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  try {
    manifest = manifestFromJson(Json::parse(ss.str()));
    return true;
  } catch (...) {
    return false;
  }
}

bool saveChannelPage(
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  const ChannelPage &page
) {
  ensureDataDir();

  std::ofstream file(channelPageCachePath(provider, type, categoryId, page.page), std::ios::binary);
  if (!file) return false;

  file << channelPageToJson(page).stringify();
  return true;
}

bool loadChannelPage(
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int pageNumber,
  ChannelPage &page
) {
  std::ifstream file(channelPageCachePath(provider, type, categoryId, pageNumber), std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  try {
    page = channelPageFromJson(Json::parse(ss.str()));
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace nstv
