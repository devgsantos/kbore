#include "nstv/parser_api_client.hpp"
#include "nstv/manifest_json.hpp"
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cctype>
#include <utility>
#include <zlib.h>

namespace nstv {

namespace {

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

void ParserApiClient::progress(const std::string &message) const {
  if (progress_) {
    progress_(message);
  }
}


std::string ParserApiClient::requestBody(const std::string &url, const std::string &body) const {
  progress("Downloading manifest...");

  HttpResponse res = http_.postJson(
    url,
    body,
    authHeaders(),
    [this](std::size_t bytes) {
      std::ostringstream message;
      message << "Downloading manifest... " << (bytes / 1024) << " KB";
      progress(message.str());
    }
  );

  if (!res.error.empty()) {
    throw std::runtime_error("HTTP request failed: " + res.error);
  }

  if (res.status < 200 || res.status >= 300) {
    std::ostringstream message;
    message << "HTTP " << res.status;
    if (!res.body.empty() && res.body.size() < 2048) {
      message << ": " << res.body;
    }
    throw std::runtime_error(message.str());
  }

  if (res.body.empty()) {
    throw std::runtime_error("Parser API returned an empty response body");
  }

  return std::move(res.body);
}

Json ParserApiClient::requestJson(const std::string &url, const std::string &body) const {
  std::string response = requestBody(url, body);

  {
    std::ostringstream message;
    message << "Parsing JSON... " << (response.size() / 1024) << " KB";
    progress(message.str());
  }

  Json json = Json::parse(response);

  progress("Validating response...");

  if (!json["ok"].asBool(false)) {
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

  if (bodyIsGzip) {
    result.cacheText = std::move(res.body);
    result.cacheTextIsGzip = true;
  } else {
    result.cacheText = std::move(responseText);
    result.cacheTextIsGzip = false;
  }

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
  node.totalItems = json["totalItems"].asInt(json["totalChannels"].asInt(0));
  node.totalChannels = json["totalChannels"].asInt(node.totalItems);
  node.playable = json["playable"].asBool(!node.url.empty());

  if (json["children"].isArray()) {
    for (const Json &childJson : json["children"].asArray()) {
      MediaNode child = nodeFromJson(childJson, node.type);
      node.children.push_back(child);
    }
  }

  if (node.kind.empty()) {
    node.kind = node.children.empty() && node.playable ? "item" : "folder";
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
      channel.name = item["name"].asString(item["title"].asString("Channel"));
      channel.url = item["url"].asString(item["playbackUrl"].asString(""));
      channel.logo = item["logo"].asString(item["stream_icon"].asString(item["cover"].asString("")));
      channel.group = item["group"].asString(item["category"].asString(""));
      channel.groupId = item["groupId"].asString(item["categoryId"].asString(item["category_id"].asString("")));
      channel.type = streamTypeFromString(item["type"].asString(item["streamType"].asString("live")));
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

} // namespace nstv
