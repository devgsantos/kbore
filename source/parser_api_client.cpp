#include "nstv/parser_api_client.hpp"
#include <sstream>
#include <stdexcept>

namespace nstv {

ParserApiClient::ParserApiClient(Config config) : config_(std::move(config)) {}

std::string ParserApiClient::endpoint(const std::string &path) const {
  if (config_.parserApiBaseUrl.empty()) {
    throw std::runtime_error("Parser API base URL is empty. Configure /switch/nstv/config.json");
  }
  return config_.parserApiBaseUrl + path;
}

std::map<std::string, std::string> ParserApiClient::authHeaders() const {
  std::map<std::string, std::string> headers;
  if (!config_.apiKey.empty()) headers["x-api-key"] = config_.apiKey;
  return headers;
}

Json ParserApiClient::requestJson(const std::string &url, const std::string &body) const {
  HttpResponse res = http_.postJson(url, body, authHeaders());
  if (!res.error.empty()) throw std::runtime_error("HTTP request failed: " + res.error);
  Json json = Json::parse(res.body);
  if (res.status < 200 || res.status >= 300) {
    std::string message = json["error"]["message"].asString("HTTP " + std::to_string(res.status));
    throw std::runtime_error(message);
  }
  if (!json["ok"].asBool(false)) {
    throw std::runtime_error(json["error"]["message"].asString(json["message"].asString("Parser API returned ok=false")));
  }
  return json;
}

Manifest ParserApiClient::loadM3uManifest(const std::string &playlistUrl) const {
  std::string body = std::string("{\"url\":\"") + jsonEscape(playlistUrl) + "\",\"mode\":\"manifest\"}";
  return loadManifestEndpoint(endpoint("/api/parse-url"), body, Provider::M3u);
}

Manifest ParserApiClient::loadXtreamManifest(const std::string &xtreamUrl) const {
  std::string body = std::string("{\"url\":\"") + jsonEscape(xtreamUrl) + "\"}";
  return loadManifestEndpoint(endpoint("/api/xtream/manifest"), body, Provider::Xtream);
}

Manifest ParserApiClient::loadManifestEndpoint(const std::string &endpointUrl, const std::string &body, Provider provider) const {
  Json json = requestJson(endpointUrl, body);
  std::string source = json["manifest"]["source"].asString("");
  return manifestFromJson(json["manifest"], source, provider);
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
  manifest.name = json["name"].asString(sourceUrl.empty() ? "NSTV Playlist" : sourceUrl);
  manifest.source = json["source"].asString(sourceUrl);
  manifest.provider = provider;
  manifest.totalChannels = json["totalChannels"].asInt(0);

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
      manifest.types.push_back(group);
    }
  }
  return manifest;
}

ChannelPage ParserApiClient::channelPageFromJson(const Json &json) const {
  ChannelPage page;
  page.page = json["page"].asInt(1);
  page.pageSize = json["pageSize"].asInt(config_.pageSize);
  page.totalChannels = json["totalChannels"].asInt(json["total"].asInt(0));
  page.totalPages = json["totalPages"].asInt(1);
  page.hasNextPage = json["hasNextPage"].asBool(page.page < page.totalPages);

  const Json &items = json["channels"].isArray() ? json["channels"] : json["items"];
  if (items.isArray()) {
    for (const Json &item : items.asArray()) {
      Channel channel;
      channel.id = item["id"].asString("");
      channel.name = item["name"].asString("Channel");
      channel.url = item["url"].asString("");
      channel.logo = item["logo"].asString("");
      channel.group = item["group"].asString("");
      channel.groupId = item["groupId"].asString(item["categoryId"].asString(""));
      channel.type = streamTypeFromString(item["type"].asString("live"));
      page.channels.push_back(channel);
    }
  }
  return page;
}

} // namespace nstv
