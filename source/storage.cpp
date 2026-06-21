#include "nstv/storage.hpp"
#include "nstv/manifest_json.hpp"
#include "nstv/json.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <set>
#include <thread>
#include <chrono>
#include <vector>
#include <sys/stat.h>
#include <zlib.h>

namespace nstv {

static std::string safeFilePart(const std::string &value);

std::string dataDir() {
#ifdef __SWITCH__
  return "sdmc:/switch/kbore";
#else
  return ".";
#endif
}

std::string activeManifestPath() {
  return dataDir() + "/manifests/active-manifest.json";
}

std::string manifestPath(const std::string &playlistId) {
  return dataDir() + "/manifests/manifest-" + safeFilePart(playlistId.empty() ? "active" : playlistId) + ".json";
}

static std::string legacyActiveManifestPath() {
  return dataDir() + "/active-manifest.json";
}

static std::string legacyManifestPath(const std::string &playlistId) {
  return dataDir() + "/manifest-" + safeFilePart(playlistId.empty() ? "active" : playlistId) + ".json";
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
  mkdir("sdmc:/switch/kbore", 0777);
  mkdir("sdmc:/switch/kbore/cache", 0777);
  mkdir("sdmc:/switch/kbore/manifests", 0777);
  mkdir("sdmc:/switch/kbore/favorites", 0777);
#else
  mkdir(".", 0777);
  mkdir("./cache", 0777);
  mkdir("./manifests", 0777);
  mkdir("./favorites", 0777);
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

std::string playlistManifestPath(const std::string &playlistId) {
  return dataDir() + "/manifests/" + safeFilePart(playlistId.empty() ? "active" : playlistId) + "_manifest.json";
}

static std::string playlistManifestGzipPath(const std::string &playlistId) {
  return playlistManifestPath(playlistId) + ".gz";
}

static std::string legacyPlaylistManifestPath(const std::string &playlistId) {
  return cacheDir() + "/" + safeFilePart(playlistId.empty() ? "active" : playlistId) + "_manifest.json";
}

static std::string legacyPlaylistManifestGzipPath(const std::string &playlistId) {
  return legacyPlaylistManifestPath(playlistId) + ".gz";
}

std::string channelPageCachePath(
  const std::string &playlistId,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int page
) {
  return cacheDir() + "/" +
    safeFilePart(playlistId.empty() ? "active" : playlistId) + "_" +
    safeFilePart(toString(provider)) + "_" +
    safeFilePart(toString(type)) + "_" +
    safeFilePart(categoryId) + "_page_" +
    std::to_string(page) + ".json";
}

std::string epgCacheKey(const Channel &channel) {
  if (!channel.tvgId.empty()) return channel.tvgId;
  if (!channel.streamId.empty()) return channel.streamId;
  if (!channel.id.empty()) return channel.id;
  return channel.name;
}

std::string epgCachePath(const std::string &playlistId, const Channel &channel) {
  return cacheDir() + "/" +
    safeFilePart(playlistId.empty() ? "active" : playlistId) + "_epg_" +
    safeFilePart(epgCacheKey(channel)) + ".json";
}

std::string vodDetailsCacheKey(const Channel &channel) {
  if (!channel.streamId.empty()) return channel.streamId;
  if (!channel.id.empty()) return channel.id;
  if (!channel.tvgId.empty()) return channel.tvgId;
  return channel.name;
}

std::string vodDetailsCachePath(const std::string &playlistId, const Channel &channel) {
  return cacheDir() + "/" +
    safeFilePart(playlistId.empty() ? "active" : playlistId) + "_vod_" +
    safeFilePart(toString(channel.type)) + "_" +
    safeFilePart(vodDetailsCacheKey(channel)) + ".json";
}

std::string nodeChildrenPageCachePath(
  const std::string &playlistId,
  const std::string &nodeId,
  int page
) {
  return cacheDir() + "/" +
    safeFilePart(playlistId.empty() ? "active" : playlistId) + "_node_" +
    safeFilePart(nodeId) + "_page_" +
    std::to_string(page) + ".json";
}


static Json mediaNodeToJson(const MediaNode &node) {
  Json json(Json::object_t{});
  json["id"] = node.id;
  json["title"] = node.title;
  json["name"] = node.name;
  json["type"] = node.type;
  json["kind"] = node.kind;
  json["url"] = node.url;
  json["logo"] = node.logo;
  json["group"] = node.group;
  json["groupId"] = node.groupId;
  json["tvgId"] = node.tvgId;
  json["tvgName"] = node.tvgName;
  json["streamId"] = node.streamId;
  json["totalItems"] = node.totalItems;
  json["totalChannels"] = node.totalChannels;
  json["childCount"] = node.childCount;
  json["hasChildren"] = node.hasChildren;
  json["playable"] = node.playable;

  Json::array_t children;
  for (const auto &child : node.children) {
    children.push_back(mediaNodeToJson(child));
  }

  json["children"] = Json(children);
  return json;
}

static MediaNode mediaNodeFromJson(const Json &json, const std::string &fallbackType = "") {
  MediaNode node;
  node.id = json["id"].asString("");
  node.title = json["title"].asString(json["name"].asString(node.id.empty() ? "Untitled" : node.id));
  node.name = json["name"].asString(node.title);
  node.type = json["type"].asString(json["streamType"].asString(fallbackType));
  if (node.type.empty()) {
    node.type = "live";
  }

  node.kind = json["kind"].asString("");
  node.url = json["url"].asString(json["playbackUrl"].asString(""));
  node.logo = json["logo"].asString(json["stream_icon"].asString(json["cover"].asString("")));
  node.group = json["group"].asString(json["category"].asString(""));
  node.groupId = json["groupId"].asString(json["categoryId"].asString(json["category_id"].asString("")));
  node.tvgId = json["tvgId"].asString(json["tvg-id"].asString(json["epgChannelId"].asString("")));
  node.tvgName = json["tvgName"].asString(json["tvg-name"].asString(json["name"].asString("")));
  if (json["streamId"].isString()) {
    node.streamId = json["streamId"].asString("");
  } else {
    int parsedStreamId = json["streamId"].asInt(json["stream_id"].asInt(0));
    node.streamId = parsedStreamId > 0 ? std::to_string(parsedStreamId) : "";
  }
  node.totalItems = json["totalItems"].asInt(json["totalChannels"].asInt(0));
  node.totalChannels = json["totalChannels"].asInt(node.totalItems);
  node.childCount = json["childCount"].asInt(json["childrenCount"].asInt(json["count"].asInt(0)));
  node.hasChildren = json["hasChildren"].asBool(node.childCount > 0);
  node.playable = json["playable"].asBool(!node.url.empty());

  if (json["children"].isArray()) {
    for (const auto &childJson : json["children"].asArray()) {
      node.children.push_back(mediaNodeFromJson(childJson, node.type));
    }
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

  return node;
}

static Json manifestToJson(const Manifest &manifest) {
  Json root(Json::object_t{});
  root["id"] = manifest.id;
  root["name"] = manifest.name;
  root["source"] = manifest.source;
  root["provider"] = toString(manifest.provider);
  root["totalChannels"] = manifest.totalChannels;
  root["totalItems"] = manifest.totalItems;

  Json::array_t nodes;
  for (const auto &node : manifest.nodes) {
    nodes.push_back(mediaNodeToJson(node));
  }
  root["nodes"] = Json(nodes);

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

static Manifest manifestFromJson(const Json &input) {
  const Json &json = input["manifest"].isObject()
    ? input["manifest"]
    : (input["data"]["manifest"].isObject() ? input["data"]["manifest"] : input);

  Manifest manifest;
  manifest.id = json["id"].asString("cached-playlist");
  manifest.name = json["name"].asString("Cached Playlist");
  manifest.source = json["source"].asString("");
  manifest.provider = providerFromString(json["provider"].asString("local"));
  manifest.totalChannels = json["totalChannels"].asInt(json["totalItems"].asInt(0));
  manifest.totalItems = json["totalItems"].asInt(manifest.totalChannels);

  if (json["nodes"].isArray()) {
    for (const auto &nodeJson : json["nodes"].asArray()) {
      manifest.nodes.push_back(mediaNodeFromJson(nodeJson));
    }
  }

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

static bool readManifestFile(const std::string &path, Manifest &manifest) {
  std::ifstream file(path, std::ios::binary);
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

static Json channelToJson(const Channel &channel) {
  Json json(Json::object_t{});
  json["id"] = channel.id;
  json["name"] = channel.name;
  json["url"] = channel.url;
  json["logo"] = channel.logo;
  json["group"] = channel.group;
  json["groupId"] = channel.groupId;
  json["tvgId"] = channel.tvgId;
  json["tvgName"] = channel.tvgName;
  json["streamId"] = channel.streamId;
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
  channel.tvgId = json["tvgId"].asString("");
  channel.tvgName = json["tvgName"].asString("");
  channel.streamId = json["streamId"].asString("");
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

static Json nodeChildrenPageToJson(const NodeChildrenPage &page) {
  Json root(Json::object_t{});
  root["page"] = page.page;
  root["pageSize"] = page.pageSize;
  root["totalItems"] = page.totalItems;
  root["totalPages"] = page.totalPages;
  root["hasNextPage"] = page.hasNextPage;

  Json::array_t items;
  for (const auto &item : page.items) {
    items.push_back(mediaNodeToJson(item));
  }
  root["items"] = Json(items);

  return root;
}

static NodeChildrenPage nodeChildrenPageFromJson(const Json &json) {
  const Json &root = json["data"].isObject() ? json["data"] : json;

  NodeChildrenPage page;
  page.page = root["page"].asInt(json["page"].asInt(1));
  page.pageSize = root["pageSize"].asInt(json["pageSize"].asInt(100));
  page.totalItems = root["totalItems"].asInt(root["totalChannels"].asInt(root["total"].asInt(0)));
  page.totalPages = root["totalPages"].asInt(json["totalPages"].asInt(1));
  page.hasNextPage = root["hasNextPage"].asBool(json["hasNextPage"].asBool(page.page < page.totalPages));

  const Json &items = root["items"].isArray()
    ? root["items"]
    : (root["children"].isArray() ? root["children"] : json["items"]);

  if (items.isArray()) {
    for (const auto &item : items.asArray()) {
      page.items.push_back(mediaNodeFromJson(item));
    }
  }

  if (page.totalItems <= 0 && !page.items.empty()) {
    page.totalItems = static_cast<int>(page.items.size());
  }

  if (page.totalPages <= 0) {
    page.totalPages = 1;
  }

  return page;
}

static bool saveManifestToPath(const Manifest &manifest, const std::string &path) {
  ensureDataDir();

  std::ofstream file(path, std::ios::binary);
  if (!file) return false;

  /*
    Dynamic node manifests can be very large. Avoid creating a full Json DOM
    and then stringify() recursively; stream the JSON directly to disk.
  */
  writeManifestJson(file, manifest);
  return true;
}
static bool saveManifestTextRawToPath(
  const std::string &manifestText,
  const std::string &path,
  const std::function<void(std::size_t written, std::size_t total)> &progress
) {
  ensureDataDir();

  std::ofstream file(path, std::ios::binary);
  if (!file) return false;

  constexpr std::size_t chunkSize = 16 * 1024;
  const std::size_t total = manifestText.size();
  std::size_t written = 0;
  int chunkIndex = 0;

  while (written < total) {
    const std::size_t remaining = total - written;
    const std::size_t count = remaining < chunkSize ? remaining : chunkSize;

    file.write(manifestText.data() + written, static_cast<std::streamsize>(count));

    if (!file) {
      return false;
    }

    written += count;

    if (progress) {
      progress(written, total);
    }

    if ((++chunkIndex % 4) == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  file.flush();
  return static_cast<bool>(file);
}

static bool saveManifestTextGzipToPath(
  const std::string &manifestText,
  const std::string &path,
  const std::function<void(std::size_t written, std::size_t total)> &progress
) {
  ensureDataDir();

  const std::string tempPath = path + ".tmp";
  std::remove(tempPath.c_str());

  gzFile file = gzopen(tempPath.c_str(), "wb1");

  if (!file) {
    return false;
  }

  constexpr std::size_t chunkSize = 16 * 1024;
  const std::size_t total = manifestText.size();
  std::size_t written = 0;
  int chunkIndex = 0;

  while (written < total) {
    const std::size_t remaining = total - written;
    const std::size_t count = remaining < chunkSize ? remaining : chunkSize;

    const int result = gzwrite(
      file,
      manifestText.data() + written,
      static_cast<unsigned int>(count)
    );

    if (result <= 0) {
      gzclose(file);
      std::remove(tempPath.c_str());
      return false;
    }

    written += static_cast<std::size_t>(result);

    if (progress) {
      progress(written, total);
    }

    /*
      Writing/compressing tens of MB can otherwise monopolize the Switch CPU/SD
      path enough to make the UI look frozen. Yield periodically so the main
      thread keeps animating the spinner.
    */
    if ((++chunkIndex % 4) == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  if (gzclose(file) != Z_OK) {
    std::remove(tempPath.c_str());
    return false;
  }

  std::remove(path.c_str());

  if (std::rename(tempPath.c_str(), path.c_str()) != 0) {
    std::remove(tempPath.c_str());
    return false;
  }

  return true;
}

static bool readGzipTextFile(const std::string &path, std::string &out) {
  gzFile file = gzopen(path.c_str(), "rb");

  if (!file) {
    return false;
  }

  out.clear();

  constexpr std::size_t chunkSize = 64 * 1024;
  std::vector<char> buffer(chunkSize);

  while (true) {
    const int read = gzread(file, buffer.data(), static_cast<unsigned int>(buffer.size()));

    if (read > 0) {
      out.append(buffer.data(), static_cast<std::size_t>(read));
      continue;
    }

    if (read == 0) {
      break;
    }

    gzclose(file);
    out.clear();
    return false;
  }

  return gzclose(file) == Z_OK;
}


static bool parseManifestText(Manifest &manifest, const std::string &text) {
  try {
    /*
      Accept both cached flattened manifests and API responses with
      { ok, manifest }. Parse directly into the Manifest model because large
      node trees are too expensive for the generic Json DOM on Switch.
    */
    manifest = manifestFromJsonTextFast(text);
    return !manifest.nodes.empty() || !manifest.types.empty();
  } catch (...) {
    return false;
  }
}

static bool loadManifestFromPath(Manifest &manifest, const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  return parseManifestText(manifest, ss.str());
}

static bool loadManifestFromGzipPath(Manifest &manifest, const std::string &path) {
  std::string text;

  if (!readGzipTextFile(path, text)) {
    return false;
  }

  return parseManifestText(manifest, text);
}

static bool copyFile(const std::string &from, const std::string &to) {
  ensureDataDir();

  std::ifstream input(from, std::ios::binary);
  if (!input) return false;

  std::ofstream output(to, std::ios::binary);
  if (!output) return false;

  output << input.rdbuf();
  return static_cast<bool>(output);
}

bool saveManifestForPlaylist(const Manifest &manifest, const std::string &playlistId) {
  return saveManifestToPath(manifest, playlistManifestPath(playlistId.empty() ? manifest.id : playlistId));
}

bool loadManifestForPlaylist(Manifest &manifest, const std::string &playlistId) {
  const std::string gzipPath = playlistManifestGzipPath(playlistId);
  const std::string rawPath = playlistManifestPath(playlistId);

  if (!loadManifestFromGzipPath(manifest, gzipPath) &&
      !loadManifestFromPath(manifest, rawPath)) {
    const std::string legacyGzipPath = legacyPlaylistManifestGzipPath(playlistId);
    const std::string legacyRawPath = legacyPlaylistManifestPath(playlistId);

    if (loadManifestFromGzipPath(manifest, legacyGzipPath)) {
      copyFile(legacyGzipPath, gzipPath);
    } else if (loadManifestFromPath(manifest, legacyRawPath)) {
      copyFile(legacyRawPath, rawPath);
    } else {
      return false;
    }
  }

  /*
    Per-playlist cache paths already identify the owner playlist. Raw API
    responses may contain their own generic id, so normalize it here to keep
    the existing active-playlist comparison working.
  */
  if (!playlistId.empty()) {
    manifest.id = playlistId;
  }

  return true;
}


bool saveManifestGzipBytesForPlaylist(
  const std::string &gzipBytes,
  const std::string &playlistId,
  const std::function<void(std::size_t written, std::size_t total)> &progress
) {
  /*
    The parser API can already return gzip-compressed manifests. Prefer
    storing those bytes directly instead of decompressing + recompressing on
    the Switch. This keeps CPU usage low and makes the SD write much smaller.
  */
  if (gzipBytes.empty()) {
    return false;
  }

  const std::string gzipPath = playlistManifestGzipPath(playlistId);
  const std::string tempPath = gzipPath + ".tmp";

  std::remove(tempPath.c_str());

  if (!saveManifestTextRawToPath(gzipBytes, tempPath, progress)) {
    std::remove(tempPath.c_str());
    return false;
  }

  std::remove(gzipPath.c_str());

  if (std::rename(tempPath.c_str(), gzipPath.c_str()) != 0) {
    std::remove(tempPath.c_str());
    return false;
  }

  std::remove(playlistManifestPath(playlistId).c_str());
  return true;
}

bool saveManifestTextForPlaylist(
  const std::string &manifestText,
  const std::string &playlistId,
  const std::function<void(std::size_t written, std::size_t total)> &progress
) {
  /*
    Save large dynamic manifests compressed on the SD card. This avoids long,
    blocking writes of tens of MB and keeps subsequent playlist switches much
    faster. Old raw .json caches are still supported by loadManifestForPlaylist().
  */
  const std::string gzipPath = playlistManifestGzipPath(playlistId);

  if (saveManifestTextGzipToPath(manifestText, gzipPath, progress)) {
    std::remove(playlistManifestPath(playlistId).c_str());
    return true;
  }

  return saveManifestTextRawToPath(
    manifestText,
    playlistManifestPath(playlistId),
    progress
  );
}

bool saveManifest(const Manifest &manifest) {
  return saveManifestToPath(manifest, activeManifestPath());
}

bool loadManifest(Manifest &manifest) {
  return readManifestFile(activeManifestPath(), manifest) ||
    readManifestFile(legacyActiveManifestPath(), manifest);
}

bool loadManifest(const std::string &playlistId, Manifest &manifest) {
  if (readManifestFile(manifestPath(playlistId), manifest)) {
    return true;
  }

  if (readManifestFile(legacyManifestPath(playlistId), manifest)) {
    saveManifest(manifest);
    return true;
  }

  Manifest legacy;
  if (
    !playlistId.empty() &&
    readManifestFile(legacyActiveManifestPath(), legacy) &&
    legacy.id == playlistId &&
    !legacy.types.empty()
  ) {
    manifest = legacy;
    saveManifest(manifest);
    return true;
  }

  return false;
}

bool saveChannelPage(
  const std::string &playlistId,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  const ChannelPage &page
) {
  ensureDataDir();

  std::ofstream file(channelPageCachePath(playlistId, provider, type, categoryId, page.page), std::ios::binary);
  if (!file) return false;

  file << channelPageToJson(page).stringify();
  return true;
}

bool loadChannelPage(
  const std::string &playlistId,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int pageNumber,
  ChannelPage &page
) {
  std::ifstream file(channelPageCachePath(playlistId, provider, type, categoryId, pageNumber), std::ios::binary);
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


static Json epgProgramToJson(const EpgProgram &program) {
  Json json(Json::object_t{});
  json["channelId"] = program.channelId;
  json["channelName"] = program.channelName;
  json["title"] = program.title;
  json["description"] = program.description;
  json["start"] = program.start;
  json["stop"] = program.stop;
  return json;
}

static EpgProgram epgProgramFromJson(const Json &json) {
  EpgProgram program;
  program.channelId = json["channelId"].asString("");
  program.channelName = json["channelName"].asString("");
  program.title = json["title"].asString("Program");
  program.description = json["description"].asString("");
  program.start = json["start"].asString("");
  program.stop = json["stop"].asString("");
  return program;
}

static Json epgPageToJson(const EpgPage &page) {
  Json root(Json::object_t{});
  root["page"] = page.page;
  root["pageSize"] = page.pageSize;
  root["totalPrograms"] = page.totalPrograms;
  root["totalPages"] = page.totalPages;
  root["hasNextPage"] = page.hasNextPage;

  Json::array_t programs;
  for (const auto &program : page.programs) {
    programs.push_back(epgProgramToJson(program));
  }
  root["programs"] = Json(programs);
  return root;
}

static EpgPage epgPageFromJson(const Json &json) {
  EpgPage page;
  page.page = json["page"].asInt(1);
  page.pageSize = json["pageSize"].asInt(12);
  page.totalPrograms = json["totalPrograms"].asInt(json["total"].asInt(0));
  page.totalPages = json["totalPages"].asInt(1);
  page.hasNextPage = json["hasNextPage"].asBool(page.page < page.totalPages);

  const Json &programsJson = json["programs"].isArray() ? json["programs"] : json["items"];
  if (programsJson.isArray()) {
    for (const auto &item : programsJson.asArray()) {
      page.programs.push_back(epgProgramFromJson(item));
    }
  }

  if (page.totalPrograms == 0) {
    page.totalPrograms = static_cast<int>(page.programs.size());
  }

  return page;
}

bool saveEpgPage(
  const std::string &playlistId,
  const Channel &channel,
  const EpgPage &page
) {
  ensureDataDir();

  std::ofstream file(epgCachePath(playlistId, channel), std::ios::binary);
  if (!file) return false;

  file << epgPageToJson(page).stringify();
  return true;
}

bool loadEpgPage(
  const std::string &playlistId,
  const Channel &channel,
  EpgPage &page
) {
  std::ifstream file(epgCachePath(playlistId, channel), std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  try {
    page = epgPageFromJson(Json::parse(ss.str()));
    return true;
  } catch (...) {
    return false;
  }
}

static Json vodDetailsToJson(const VodDetails &details) {
  Json json(Json::object_t{});
  json["title"] = details.title;
  json["description"] = details.description;
  json["posterUrl"] = details.posterUrl;
  json["backdropUrl"] = details.backdropUrl;
  json["releaseDate"] = details.releaseDate;
  json["provider"] = details.provider;
  return json;
}

static VodDetails vodDetailsFromJson(const Json &json) {
  const Json &root = json["details"].isObject() ? json["details"] : json;
  VodDetails details;
  details.title = root["title"].asString(root["name"].asString(""));
  details.description = root["description"].asString(root["desc"].asString(root["plot"].asString(root["overview"].asString(root["synopsis"].asString("")))));
  details.posterUrl = root["posterUrl"].asString(root["poster_url"].asString(root["image"].asString(root["stream_icon"].asString(root["cover"].asString(root["poster"].asString(""))))));
  details.backdropUrl = root["backdropUrl"].asString(root["backdrop_url"].asString(root["backdrop_path"].asString("")));
  details.releaseDate = root["releaseDate"].asString(root["release_date"].asString(root["firstAirDate"].asString(root["first_air_date"].asString(""))));
  details.provider = root["provider"].asString("");
  return details;
}

bool saveVodDetails(const std::string &playlistId, const Channel &channel, const VodDetails &details) {
  ensureDataDir();
  std::ofstream file(vodDetailsCachePath(playlistId, channel), std::ios::binary);
  if (!file) return false;
  file << vodDetailsToJson(details).stringify();
  return true;
}

bool loadVodDetails(const std::string &playlistId, const Channel &channel, VodDetails &details) {
  std::ifstream file(vodDetailsCachePath(playlistId, channel), std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  try {
    details = vodDetailsFromJson(Json::parse(ss.str()));
    return true;
  } catch (...) {
    return false;
  }
}

bool saveNodeChildrenPage(
  const std::string &playlistId,
  const std::string &nodeId,
  const NodeChildrenPage &page
) {
  ensureDataDir();

  std::ofstream file(nodeChildrenPageCachePath(playlistId, nodeId, page.page), std::ios::binary);
  if (!file) return false;

  file << nodeChildrenPageToJson(page).stringify();
  return true;
}

bool loadNodeChildrenPage(
  const std::string &playlistId,
  const std::string &nodeId,
  int pageNumber,
  NodeChildrenPage &page
) {
  std::ifstream file(nodeChildrenPageCachePath(playlistId, nodeId, pageNumber), std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  try {
    // page = epgPageFromJson(Json::parse(ss.str()));
    page = nodeChildrenPageFromJson(Json::parse(ss.str()));
    return true;
  } catch (...) {
    return false;
  }
}

std::string favoritesDir() {
  return dataDir() + "/favorites";
}

std::string favoritesPathForPlaylist(const std::string &playlistId) {
  return favoritesDir() + "/" + safeFilePart(playlistId.empty() ? "active" : playlistId) + ".json";
}

static Json favoriteChannelToJson(const Channel &channel) {
  Json json(Json::object_t{});
  json["favorite_id"] = channel.id;
  json["id"] = channel.id;
  json["name"] = channel.name;
  json["url"] = channel.url;
  json["logo"] = channel.logo;
  json["group"] = channel.group;
  json["groupId"] = channel.groupId;
  json["tvgId"] = channel.tvgId;
  json["tvgName"] = channel.tvgName;
  json["streamId"] = channel.streamId;
  json["type"] = toString(channel.type);
  return json;
}

static Channel favoriteChannelFromJson(const Json &json) {
  Channel channel;
  channel.id = json["favorite_id"].asString(json["id"].asString(""));
  channel.name = json["name"].asString(json["title"].asString("Untitled"));
  channel.url = json["url"].asString(json["playbackUrl"].asString(""));
  channel.logo = json["logo"].asString(json["stream_icon"].asString(json["cover"].asString("")));
  channel.group = json["group"].asString(json["category"].asString(""));
  channel.groupId = json["groupId"].asString(json["categoryId"].asString(json["category_id"].asString("")));
  channel.tvgId = json["tvgId"].asString(json["tvg-id"].asString(json["epgChannelId"].asString("")));
  channel.tvgName = json["tvgName"].asString(json["tvg-name"].asString(channel.name));
  if (json["streamId"].isString()) {
    channel.streamId = json["streamId"].asString("");
  } else {
    int parsedStreamId = json["streamId"].asInt(json["stream_id"].asInt(0));
    channel.streamId = parsedStreamId > 0 ? std::to_string(parsedStreamId) : "";
  }
  channel.type = streamTypeFromString(json["type"].asString(json["streamType"].asString("live")));
  return channel;
}

bool saveFavoritesForPlaylist(const std::string &playlistId, const std::vector<Channel> &favorites) {
  ensureDataDir();

  Json::array_t items;
  for (const Channel &channel : favorites) {
    if (channel.url.empty()) {
      continue;
    }
    items.push_back(favoriteChannelToJson(channel));
  }

  Json root(Json::object_t{});
  root["version"] = 1;
  root["playlist_id"] = playlistId;
  root["items"] = Json(items);

  std::ofstream file(favoritesPathForPlaylist(playlistId), std::ios::binary);
  if (!file) return false;

  file << root.stringify();
  return true;
}

bool loadFavoritesForPlaylist(const std::string &playlistId, std::vector<Channel> &favorites) {
  favorites.clear();

  std::ifstream file(favoritesPathForPlaylist(playlistId), std::ios::binary);
  if (!file) return false;

  std::ostringstream ss;
  ss << file.rdbuf();

  try {
    Json parsed = Json::parse(ss.str());
    const Json *itemsJson = &parsed;

    if (parsed.isObject() && parsed["items"].isArray()) {
      itemsJson = &parsed["items"];
    }

    if (!itemsJson->isArray()) {
      return false;
    }

    std::set<std::string> seen;
    for (const Json &item : itemsJson->asArray()) {
      Channel channel = favoriteChannelFromJson(item);
      if (channel.url.empty()) {
        continue;
      }
      if (channel.id.empty()) {
        channel.id = channel.type == StreamType::Live
          ? (channel.tvgId.empty() ? channel.streamId : channel.tvgId)
          : channel.name;
      }
      const std::string key = channel.id.empty() ? channel.url : channel.id;
      if (seen.count(key)) {
        continue;
      }
      seen.insert(key);
      favorites.push_back(channel);
    }

    return true;
  } catch (...) {
    favorites.clear();
    return false;
  }
}

} // namespace nstv
