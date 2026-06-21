#include "nstv/parser_api_client.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include "nstv/manifest_json.hpp"
#include "nstv/platform.hpp"
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cctype>
#include <utility>
#include <zlib.h>

namespace nstv {

namespace {

std::string isoLocalTime(std::time_t value) {
  PlatformLocalTime localTime;
  if (!localTimeFromUnix(value, localTime)) {
    return "";
  }

  const int offsetSeconds = localTime.utcOffsetSeconds;
  const char offsetSign = offsetSeconds < 0 ? '-' : '+';
  const int absoluteOffset = offsetSeconds < 0 ? -offsetSeconds : offsetSeconds;
  const int offsetHours = absoluteOffset / 3600;
  const int offsetMinutes = (absoluteOffset % 3600) / 60;

  char buffer[96] = {};
  std::snprintf(
    buffer,
    sizeof(buffer),
    "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
    localTime.year,
    localTime.month,
    localTime.day,
    localTime.hour,
    localTime.minute,
    localTime.second,
    offsetSign,
    offsetHours,
    offsetMinutes
  );

  return buffer;
}


std::string currentEpgWindowJson(int epgOffsetMinutes = 0) {
  const std::time_t now = currentUnixTime();
  const long long offsetSeconds = static_cast<long long>(epgOffsetMinutes) * 60LL;
  const std::time_t sourceNow = static_cast<std::time_t>(static_cast<long long>(now) - offsetSeconds);

  // Ask the parser for a window centered around the console clock. Some EPG
  // sources contain long programmes that started well before the selected
  // channel became focused; using only now-30m can miss the active programme
  // and the UI then receives the next future item.
  // When the playlist has a manual EPG offset, ask the parser for the shifted
  // source-time window. The app will apply the inverse correction when matching
  // and displaying programmes.
  const std::string nowIso = isoLocalTime(sourceNow);
  const std::string from = isoLocalTime(sourceNow - 8 * 60 * 60);
  const std::string to = isoLocalTime(sourceNow + 16 * 60 * 60);

  if (nowIso.empty() || from.empty() || to.empty()) {
    return "";
  }

  return ",\"now\":\"" + jsonEscape(nowIso) + "\",\"nowEpoch\":" + std::to_string(static_cast<long long>(sourceNow)) + ",\"deviceNowEpoch\":" + std::to_string(static_cast<long long>(now)) + ",\"epgOffsetMinutes\":" + std::to_string(epgOffsetMinutes) + ",\"from\":\"" + jsonEscape(from) + "\",\"to\":\"" + jsonEscape(to) + "\"";
}

bool isAllDigits(const std::string &value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch));
  });
}

std::string jsonStringOrNumber(const Json &value, const std::string &fallback = "") {
  if (value.isString()) {
    return value.asString("");
  }

  if (value.isNumber()) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.0f", value.asNumber(0));
    return buffer;
  }

  return fallback;
}

bool isGzipEncoding(const std::string &encoding) {
  std::string lower;
  lower.reserve(encoding.size());

  for (char ch : encoding) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }

  return lower.find("gzip") != std::string::npos;
}

std::string gunzipBytes(const std::string &gzipBytes) {
  if (gzipBytes.empty()) {
    return {};
  }

  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(gzipBytes.data()));
  stream.avail_in = static_cast<uInt>(gzipBytes.size());

  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
    throw std::runtime_error("Could not initialize gzip inflater");
  }

  std::string output;
  output.reserve(gzipBytes.size() * 4);

  constexpr std::size_t chunkSize = 128 * 1024;
  std::string buffer(chunkSize, '\0');

  int ret = Z_OK;

  while (ret != Z_STREAM_END) {
    stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());

    ret = inflate(&stream, Z_NO_FLUSH);

    if (ret != Z_OK && ret != Z_STREAM_END) {
      inflateEnd(&stream);
      throw std::runtime_error("Could not inflate gzip manifest");
    }

    const std::size_t produced = buffer.size() - stream.avail_out;

    if (produced > 0) {
      output.append(buffer.data(), produced);
    }
  }

  inflateEnd(&stream);
  return output;
}

} // namespace

ParserApiClient::ParserApiClient(Config config, ProgressCallback progress)
  : config_(std::move(config)), progress_(std::move(progress)) {}

void ParserApiClient::progress(const std::string &message) const {
  if (progress_) {
    progress_(message);
  }
}

std::string ParserApiClient::endpoint(const std::string &path) const {
  if (config_.parserApiBaseUrl.empty()) {
    throw std::runtime_error("Internal parser API base URL is empty.");
  }
  return config_.parserApiBaseUrl + path;
}

std::map<std::string, std::string> ParserApiClient::authHeaders() const {
  std::map<std::string, std::string> headers;
  if (!config_.apiKey.empty()) headers["x-api-key"] = config_.apiKey;
  return headers;
}

Json ParserApiClient::requestJson(const std::string &url, const std::string &body) const {
  if (url.find("/epg/") != std::string::npos) {
    std::printf("[KBORE] Parser API EPG POST %s\n", url.c_str());
  }
  HttpResponse res = http_.postJson(url, body, authHeaders());
  if (!res.error.empty()) throw std::runtime_error("HTTP request failed: " + res.error);
  Json json = Json::parse(res.body);
  if (res.status < 200 || res.status >= 300) {
    std::string message = json["error"]["message"].asString("HTTP " + std::to_string(res.status));
    throw std::runtime_error(message);
  }
  if (!json["ok"].asBool(json["success"].asBool(false))) {
    throw std::runtime_error(json["error"]["message"].asString(json["message"].asString("Parser API returned ok=false")));
  }
  return json;
}

Manifest ParserApiClient::loadM3uManifest(const std::string &playlistUrl) const {
  return loadM3uManifestWithCacheText(playlistUrl).manifest;
}

Manifest ParserApiClient::loadXtreamManifest(const std::string &xtreamUrl) const {
  return loadXtreamManifestWithCacheText(xtreamUrl).manifest;
}

ManifestLoadResult ParserApiClient::loadM3uManifestWithCacheText(const std::string &playlistUrl) const {
  std::string body = std::string("{\"url\":\"") + jsonEscape(playlistUrl) + "\",\"mode\":\"manifest\"}";
  return loadManifestEndpointWithCacheText(endpoint("/api/nodes/parse-url"), body, Provider::M3u);
}

ManifestLoadResult ParserApiClient::loadXtreamManifestWithCacheText(const std::string &xtreamUrl) const {
  std::string body = std::string("{\"url\":\"") + jsonEscape(xtreamUrl) + "\"}";
  return loadManifestEndpointWithCacheText(endpoint("/api/nodes/xtream/manifest"), body, Provider::Xtream);
}

Manifest ParserApiClient::loadManifestEndpoint(const std::string &endpointUrl, const std::string &body, Provider provider) const {
  return loadManifestEndpointWithCacheText(endpointUrl, body, provider).manifest;
}

ManifestLoadResult ParserApiClient::loadManifestEndpointWithCacheText(
  const std::string &endpointUrl,
  const std::string &body,
  Provider provider
) const {
  progress("Downloading manifest...");

  /*
    For large dynamic manifests, request the raw gzip response instead of
    letting libcurl auto-decode it. This lets the app save the exact compressed
    payload to SD without recompressing tens of MB on the Switch.
  */
  HttpResponse res = http_.postJson(
    endpointUrl,
    body,
    authHeaders(),
    [this](std::size_t bytes) {
      std::ostringstream message;
      message << "Downloading manifest... " << (bytes / 1024) << " KB";
      progress(message.str());
    },
    false
  );

  if (!res.error.empty()) {
    throw std::runtime_error("HTTP request failed: " + res.error);
  }

  if (res.status < 200 || res.status >= 300) {
    std::ostringstream message;
    message << "HTTP " << res.status;
    throw std::runtime_error(message.str());
  }

  if (res.body.empty()) {
    throw std::runtime_error("Parser API returned an empty response body");
  }

  const bool bodyIsGzip = isGzipEncoding(res.contentEncoding);

  std::string responseText;

  if (bodyIsGzip) {
    std::ostringstream message;
    message << "Decompressing manifest... " << (res.body.size() / 1024) << " KB";
    progress(message.str());
    responseText = gunzipBytes(res.body);
  } else {
    responseText = res.body;
  }

  {
    std::ostringstream message;
    message << "Parsing manifest tree... " << (responseText.size() / 1024) << " KB";
    progress(message.str());
  }

  Manifest manifest = manifestFromJsonTextFast(responseText, provider, "");

  if (manifest.nodes.empty() && manifest.types.empty()) {
    throw std::runtime_error("Parser returned an empty manifest");
  }

  std::ostringstream message;
  message << "Manifest ready: "
          << (manifest.totalItems > 0 ? manifest.totalItems : manifest.totalChannels)
          << " items";
  progress(message.str());

  ManifestLoadResult result;
  result.manifest = std::move(manifest);

  return result;
}

ChannelPage ParserApiClient::loadChannels(
  const std::string &sourceUrl,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int page
) const {
  std::string endpointPath = provider == Provider::Xtream ? "/api/xtream/channels" : "/api/parse-url";
  return loadChannelsEndpoint(endpoint(endpointPath), sourceUrl, provider, type, categoryId, page);
}

NodeChildrenPage ParserApiClient::loadNodeChildren(
  const std::string &sourceUrl,
  Provider provider,
  StreamType type,
  const std::string &nodeId,
  int page,
  int pageSize
) const {
  std::ostringstream body;
  body << "{\"url\":\"" << jsonEscape(sourceUrl) << "\",";
  body << "\"provider\":\"" << toString(provider) << "\",";
  body << "\"nodeId\":\"" << jsonEscape(nodeId) << "\",";
  body << "\"type\":\"" << toString(type) << "\",";
  body << "\"streamType\":\"" << toString(type) << "\",";
  body << "\"page\":" << page << ",";
  body << "\"pageSize\":" << pageSize;
  body << "}";

  Json json = requestJson(endpoint("/api/nodes/children"), body.str());
  return nodeChildrenPageFromJson(json, type);
}

ChannelPage ParserApiClient::loadChannelsEndpoint(
  const std::string &endpointUrl,
  const std::string &sourceUrl,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int page
) const {
  std::ostringstream body;
  body << "{\"url\":\"" << jsonEscape(sourceUrl) << "\",";
  if (provider == Provider::M3u) body << "\"mode\":\"channels\",";
  body << "\"streamType\":\"" << toString(type) << "\",";
  body << "\"type\":\"" << toString(type) << "\",";
  body << "\"category\":\"" << jsonEscape(categoryId) << "\",";
  body << "\"page\":" << page << ",";
  body << "\"pageSize\":" << config_.pageSize;
  body << "}";

  Json json = requestJson(endpointUrl, body.str());
  return channelPageFromJson(json);
}

Manifest ParserApiClient::manifestFromJson(const Json &json, const std::string &sourceUrl, Provider provider) const {
  Manifest manifest;
  manifest.id = json["id"].asString(toString(provider) + "-playlist");
  manifest.name = json["name"].asString(sourceUrl.empty() ? "kboré Playlist" : sourceUrl);
  manifest.source = json["source"].asString(sourceUrl);
  manifest.provider = provider;
  manifest.totalChannels = json["totalChannels"].asInt(json["totalItems"].asInt(0));
  manifest.totalItems = json["totalItems"].asInt(manifest.totalChannels);

  if (json["nodes"].isArray()) {
    for (const Json &nodeJson : json["nodes"].asArray()) {
      MediaNode node = nodeFromJson(nodeJson, "");
      if (node.id.empty() && node.title.empty() && node.children.empty()) {
        continue;
      }

      manifest.nodes.push_back(node);
      appendNodeTypes(node, manifest);
    }
  }

  if (json["types"].isArray()) {
    for (const Json &typeJson : json["types"].asArray()) {
      TypeGroup group;
      group.id = streamTypeFromString(typeJson["id"].asString("live"));
      group.label = typeJson["label"].asString(toString(group.id));
      group.totalChannels = typeJson["totalChannels"].asInt(0);

      if (typeJson["categories"].isArray()) {
        for (const Json &catJson : typeJson["categories"].asArray()) {
          Category category;
          category.id = catJson["id"].asString("");
          category.name = catJson["name"].asString("Uncategorized");
          category.totalChannels = catJson["totalChannels"].asInt(0);
          category.type = streamTypeFromString(catJson["type"].asString(toString(group.id)));
          group.categories.push_back(category);
        }
      }

      bool exists = false;
      for (const auto &existing : manifest.types) {
        if (existing.id == group.id) {
          exists = true;
          break;
        }
      }

      if (!exists) {
        manifest.types.push_back(group);
      }
    }
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

MediaNode ParserApiClient::nodeFromJson(const Json &json, const std::string &fallbackType) const {
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
  node.tvgName = json["tvgName"].asString(json["tvg-name"].asString(""));
  node.streamId = json["streamId"].asString(json["stream_id"].asString(""));
  if (node.streamId.empty() && json["stream_id"].isNumber()) {
    node.streamId = std::to_string(json["stream_id"].asInt(0));
  }
  if (node.streamId.empty() && json["streamId"].isNumber()) {
    node.streamId = std::to_string(json["streamId"].asInt(0));
  }
  node.totalItems = json["totalItems"].asInt(json["totalChannels"].asInt(0));
  node.totalChannels = json["totalChannels"].asInt(node.totalItems);
  node.childCount = json["childCount"].asInt(json["childrenCount"].asInt(json["count"].asInt(0)));
  node.hasChildren = json["hasChildren"].asBool(node.childCount > 0);
  node.playable = json["playable"].asBool(!node.url.empty());

  if (json["children"].isArray()) {
    for (const Json &childJson : json["children"].asArray()) {
      MediaNode child = nodeFromJson(childJson, node.type);
      node.children.push_back(child);
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

  if (node.id.empty()) {
    node.id = node.type + ":" + node.title;
  }

  return node;
}

void ParserApiClient::appendNodeTypes(const MediaNode &node, Manifest &manifest) const {
  TypeGroup group;
  group.id = streamTypeFromString(node.type);
  group.label = node.title.empty() ? toString(group.id) : node.title;
  group.totalChannels = node.totalChannels > 0 ? node.totalChannels : node.totalItems;

  for (const auto &child : node.children) {
    Category category;
    category.id = child.id;
    category.name = child.title.empty() ? child.name : child.title;
    category.totalChannels = child.totalChannels > 0 ? child.totalChannels : child.totalItems;
    category.type = streamTypeFromString(child.type.empty() ? node.type : child.type);
    group.categories.push_back(category);
  }

  bool exists = false;
  for (const auto &existing : manifest.types) {
    if (existing.id == group.id) {
      exists = true;
      break;
    }
  }

  if (!exists) {
    manifest.types.push_back(group);
  }
}

ChannelPage ParserApiClient::channelPageFromJson(const Json &json) const {
  const Json &root =
    json["manifest"].isObject()
      ? json["manifest"]
      : (
          json["data"].isObject()
            ? json["data"]
            : json
        );

  ChannelPage page;
  page.page = root["page"].asInt(json["page"].asInt(1));
  page.pageSize = root["pageSize"].asInt(json["pageSize"].asInt(config_.pageSize));
  page.totalChannels = root["totalChannels"].asInt(root["total"].asInt(json["totalChannels"].asInt(json["total"].asInt(0))));
  page.totalPages = root["totalPages"].asInt(json["totalPages"].asInt(1));
  page.hasNextPage = root["hasNextPage"].asBool(json["hasNextPage"].asBool(page.page < page.totalPages));

  const Json &items =
    root["channels"].isArray()
      ? root["channels"]
      : (
          root["items"].isArray()
            ? root["items"]
            : (
                root["children"].isArray()
                  ? root["children"]
                  : (
                      json["channels"].isArray()
                        ? json["channels"]
                        : json["items"]
                    )
              )
        );

  if (items.isArray()) {
    for (const Json &item : items.asArray()) {
      Channel channel;
      channel.id = item["id"].asString("");
      channel.name = item["name"].asString("Channel");
      channel.url = item["url"].asString("");
      channel.logo = item["logo"].asString("");
      channel.group = item["group"].asString("");
      channel.groupId = item["groupId"].asString(item["categoryId"].asString(""));
      channel.tvgId = item["tvgId"].asString(item["tvg-id"].asString(item["epgChannelId"].asString("")));
      channel.tvgName = item["tvgName"].asString(item["tvg-name"].asString(""));
      channel.streamId = item["streamId"].asString(item["stream_id"].asString(item["id"].asString("")));
      if (channel.streamId.empty() && item["stream_id"].isNumber()) {
        channel.streamId = std::to_string(item["stream_id"].asInt(0));
      }
      if (channel.streamId.empty() && item["streamId"].isNumber()) {
        channel.streamId = std::to_string(item["streamId"].asInt(0));
      }
      channel.type = streamTypeFromString(item["type"].asString("live"));
      page.channels.push_back(channel);
    }
  }

  if (page.totalChannels <= 0 && !page.channels.empty()) {
    page.totalChannels = static_cast<int>(page.channels.size());
  }

  if (page.totalPages <= 0) {
    page.totalPages = 1;
  }

  return page;
}

NodeChildrenPage ParserApiClient::nodeChildrenPageFromJson(const Json &json, StreamType fallbackType) const {
  const Json &root =
    json["data"].isObject()
      ? json["data"]
      : json;

  NodeChildrenPage page;
  page.page = root["page"].asInt(json["page"].asInt(1));
  page.pageSize = root["pageSize"].asInt(json["pageSize"].asInt(100));
  page.totalItems = root["totalItems"].asInt(root["totalChannels"].asInt(root["total"].asInt(json["totalItems"].asInt(json["total"].asInt(0)))));
  page.totalPages = root["totalPages"].asInt(json["totalPages"].asInt(1));
  page.hasNextPage = root["hasNextPage"].asBool(json["hasNextPage"].asBool(page.page < page.totalPages));

  const Json &items =
    root["items"].isArray()
      ? root["items"]
      : (
          root["children"].isArray()
            ? root["children"]
            : (
                json["items"].isArray()
                  ? json["items"]
                  : json["children"]
              )
        );

  if (items.isArray()) {
    for (const Json &item : items.asArray()) {
      MediaNode node = nodeFromJson(item, toString(fallbackType));
      if (node.type.empty()) {
        node.type = toString(fallbackType);
      }
      page.items.push_back(std::move(node));
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


EpgPage ParserApiClient::loadEpgPrograms(
  const std::string &sourceUrl,
  Provider provider,
  const Channel &channel,
  int page,
  int pageSize,
  const std::string &manualEpgUrl,
  int epgOffsetMinutes
) const {
  if (sourceUrl.empty()) {
    throw std::runtime_error("Playlist URL/source is empty");
  }

  std::ostringstream body;
  body << "{\"url\":\"" << jsonEscape(sourceUrl) << "\",";
  body << "\"provider\":\"" << toString(provider) << "\",";
  body << "\"page\":" << page << ",";
  body << "\"pageSize\":" << pageSize;
  body << currentEpgWindowJson(epgOffsetMinutes);

  if (!manualEpgUrl.empty()) {
    body << ",\"epgUrl\":\"" << jsonEscape(manualEpgUrl) << "\"";
  }

  const std::string epgChannelId = !channel.tvgId.empty()
    ? channel.tvgId
    : (!channel.streamId.empty() ? channel.streamId : channel.id);

  if (!epgChannelId.empty()) {
    body << ",\"channelId\":\"" << jsonEscape(epgChannelId) << "\"";
  }

  if (!channel.tvgId.empty()) {
    body << ",\"tvgId\":\"" << jsonEscape(channel.tvgId) << "\"";
  }

  if (!channel.tvgName.empty()) {
    body << ",\"channelName\":\"" << jsonEscape(channel.tvgName) << "\"";
  } else if (!channel.name.empty()) {
    body << ",\"channelName\":\"" << jsonEscape(channel.name) << "\"";
  }

  if (!channel.streamId.empty()) {
    if (isAllDigits(channel.streamId)) {
      body << ",\"streamId\":" << channel.streamId;
    } else {
      body << ",\"streamId\":\"" << jsonEscape(channel.streamId) << "\"";
    }
    body << ",\"limit\":" << pageSize;
  }

  body << "}";

  const std::string path = provider == Provider::Xtream
    ? (!channel.streamId.empty() ? "/api/xtream/epg/short" : "/api/xtream/epg/programs")
    : "/api/epg/programs";

  std::printf(
    "[KBORE] EPG request path=%s channel='%s' epgChannelId='%s' tvgId='%s' tvgName='%s' streamId='%s' manualEpg=%s\n",
    path.c_str(),
    channel.name.c_str(),
    epgChannelId.c_str(),
    channel.tvgId.c_str(),
    channel.tvgName.c_str(),
    channel.streamId.c_str(),
    manualEpgUrl.empty() ? "no" : "yes"
  );

  Json json = requestJson(endpoint(path), body.str());
  EpgPage pageResult = epgPageFromJson(json);

  if (provider == Provider::Xtream && !channel.streamId.empty() && pageResult.programs.empty()) {
    std::printf(
      "[KBORE] Xtream short EPG returned no programs for streamId='%s'; trying XMLTV fallback\n",
      channel.streamId.c_str()
    );
    Json fallbackJson = requestJson(endpoint("/api/xtream/epg/programs"), body.str());
    pageResult = epgPageFromJson(fallbackJson);
  }

  return pageResult;
}

EpgPage ParserApiClient::epgPageFromJson(const Json &json) const {
  const Json &root = json["data"].isObject()
    ? json["data"]
    : (json["epg"].isObject() ? json["epg"] : json);

  EpgPage page;
  page.page = root["page"].asInt(json["page"].asInt(1));
  page.pageSize = root["pageSize"].asInt(json["pageSize"].asInt(12));
  page.totalPrograms = root["totalPrograms"].asInt(
    root["total"].asInt(
      root["totalItems"].asInt(
        json["totalPrograms"].asInt(json["total"].asInt(json["totalItems"].asInt(0)))
      )
    )
  );
  page.totalPages = root["totalPages"].asInt(json["totalPages"].asInt(1));
  page.hasNextPage = root["hasNextPage"].asBool(json["hasNextPage"].asBool(page.page < page.totalPages));

  auto appendProgramItems = [&page](const Json &items) {
    if (!items.isArray()) {
      return;
    }

    for (const Json &item : items.asArray()) {
      EpgProgram program;
      program.channelId = jsonStringOrNumber(
        item["channelId"],
        jsonStringOrNumber(item["channel"], jsonStringOrNumber(item["channel_id"], jsonStringOrNumber(item["stream_id"], "")))
      );
      program.channelName = jsonStringOrNumber(item["channelName"], jsonStringOrNumber(item["channel_name"], ""));
      program.title = jsonStringOrNumber(item["title"], jsonStringOrNumber(item["name"], "Program"));
      program.description = jsonStringOrNumber(item["description"], jsonStringOrNumber(item["desc"], ""));
      // Prefer parser-provided Unix timestamps when available. They are absolute
      // POSIX/UTC seconds and avoid XMLTV timezone ambiguity when matching NOW.
      // Raw XMLTV start/stop values remain as fallback for providers that do not
      // expose *_timestamp fields.
      program.start = jsonStringOrNumber(
        item["startEpochSeconds"],
        jsonStringOrNumber(
          item["start_timestamp"],
          jsonStringOrNumber(
            item["start_time"],
            jsonStringOrNumber(
              item["start_datetime"],
              jsonStringOrNumber(
                item["startDate"],
                jsonStringOrNumber(item["startTime"], jsonStringOrNumber(item["start"], jsonStringOrNumber(item["rawStart"], "")))
              )
            )
          )
        )
      );
      program.stop = jsonStringOrNumber(
        item["stopEpochSeconds"],
        jsonStringOrNumber(
          item["stop_timestamp"],
          jsonStringOrNumber(
            item["end_timestamp"],
            jsonStringOrNumber(
              item["stop_time"],
              jsonStringOrNumber(
                item["end_time"],
                jsonStringOrNumber(
                  item["end_datetime"],
                  jsonStringOrNumber(
                    item["stopDate"],
                    jsonStringOrNumber(
                      item["endDate"],
                      jsonStringOrNumber(
                        item["endEpochSeconds"],
                        jsonStringOrNumber(
                          item["endTime"],
                          jsonStringOrNumber(item["stopTime"], jsonStringOrNumber(item["end"], jsonStringOrNumber(item["stop"], jsonStringOrNumber(item["rawStop"], ""))))
                        )
                      )
                    )
                  )
                )
              )
            )
          )
        )
      );

      page.programs.push_back(program);
    }
  };

  auto appendProgramFields = [&appendProgramItems](const Json &container) {
    if (!container.isObject()) {
      return;
    }

    appendProgramItems(container["programs"]);
    appendProgramItems(container["programmes"]);
    appendProgramItems(container["epg_listings"]);
    appendProgramItems(container["epgListings"]);
    appendProgramItems(container["items"]);
    appendProgramItems(container["results"]);
    appendProgramItems(container["events"]);
    appendProgramItems(container["listings"]);
  };

  appendProgramFields(root);

  if (page.programs.empty()) {
    appendProgramFields(json);
  }

  if (page.programs.empty() && json["data"].isArray()) {
    appendProgramItems(json["data"]);
  }

  if (page.programs.empty() && root["data"].isArray()) {
    appendProgramItems(root["data"]);
  }

  if (page.programs.empty() && root["raw"].isObject()) {
    appendProgramFields(root["raw"]);
  }

  if (page.programs.empty() && json["raw"].isObject()) {
    appendProgramFields(json["raw"]);
  }

  auto appendObjectMappedPrograms = [&appendProgramItems, &appendProgramFields](const Json &container) {
    if (!container.isObject()) {
      return;
    }

    for (const auto &entry : container.asObject()) {
      if (entry.second.isArray()) {
        appendProgramItems(entry.second);
      } else if (entry.second.isObject()) {
        appendProgramFields(entry.second);
      }
    }
  };

  if (page.programs.empty()) {
    appendObjectMappedPrograms(root["programs"]);
    appendObjectMappedPrograms(root["programmes"]);
    appendObjectMappedPrograms(root["epg"]);
  }

  if (page.totalPrograms == 0) {
    page.totalPrograms = static_cast<int>(page.programs.size());
  }

  std::printf(
    "[KBORE] parsed EPG response: programs=%zu total=%d page=%d/%d\n",
    page.programs.size(),
    page.totalPrograms,
    page.page,
    page.totalPages
  );

  if (!page.programs.empty()) {
    const EpgProgram &first = page.programs.front();
    std::printf(
      "[KBORE] first EPG program: channelId='%s' title='%s' start='%s' stop='%s'\n",
      first.channelId.c_str(),
      first.title.c_str(),
      first.start.c_str(),
      first.stop.c_str()
    );
  }

  return page;
}

} // namespace nstv
