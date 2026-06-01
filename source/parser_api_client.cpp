#include "nstv/parser_api_client.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <ctime>

namespace nstv {

namespace {

std::string isoLocalTime(std::time_t value) {
  std::tm localTime{};
  std::tm utcTime{};

#if defined(_WIN32)
  localtime_s(&localTime, &value);
  gmtime_s(&utcTime, &value);
#else
  std::tm *result = std::localtime(&value);
  if (!result) {
    return "";
  }
  localTime = *result;

  result = std::gmtime(&value);
  if (!result) {
    return "";
  }
  utcTime = *result;
#endif

  const long offsetSeconds = static_cast<long>(std::difftime(std::mktime(&localTime), std::mktime(&utcTime)));
  const char offsetSign = offsetSeconds < 0 ? '-' : '+';
  const long absoluteOffset = offsetSeconds < 0 ? -offsetSeconds : offsetSeconds;
  const int offsetHours = static_cast<int>(absoluteOffset / 3600);
  const int offsetMinutes = static_cast<int>((absoluteOffset % 3600) / 60);

  char buffer[96] = {};
  std::snprintf(
    buffer,
    sizeof(buffer),
    "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
    localTime.tm_year + 1900,
    localTime.tm_mon + 1,
    localTime.tm_mday,
    localTime.tm_hour,
    localTime.tm_min,
    localTime.tm_sec,
    offsetSign,
    offsetHours,
    offsetMinutes
  );

  return buffer;
}

std::string currentEpgWindowJson() {
  const std::time_t now = std::time(nullptr);
  const std::string from = isoLocalTime(now - 1800);
  const std::string to = isoLocalTime(now + 24 * 60 * 60);

  if (from.empty() || to.empty()) {
    return "";
  }

  return ",\"from\":\"" + jsonEscape(from) + "\",\"to\":\"" + jsonEscape(to) + "\"";
}

bool isAllDigits(const std::string &value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch));
  });
}

} // namespace

ParserApiClient::ParserApiClient(Config config) : config_(std::move(config)) {}

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
  return page;
}


EpgPage ParserApiClient::loadEpgPrograms(
  const std::string &sourceUrl,
  Provider provider,
  const Channel &channel,
  int page,
  int pageSize,
  const std::string &manualEpgUrl
) const {
  if (sourceUrl.empty()) {
    throw std::runtime_error("Playlist URL/source is empty");
  }

  std::ostringstream body;
  body << "{\"url\":\"" << jsonEscape(sourceUrl) << "\",";
  body << "\"provider\":\"" << toString(provider) << "\",";
  body << "\"page\":" << page << ",";
  body << "\"pageSize\":" << pageSize;
  body << currentEpgWindowJson();

  if (!manualEpgUrl.empty()) {
    body << ",\"epgUrl\":\"" << jsonEscape(manualEpgUrl) << "\"";
  }

  if (!channel.tvgId.empty()) {
    body << ",\"channelId\":\"" << jsonEscape(channel.tvgId) << "\"";
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

  Json json = requestJson(endpoint(path), body.str());
  return epgPageFromJson(json);
}

EpgPage ParserApiClient::epgPageFromJson(const Json &json) const {
  EpgPage page;
  page.page = json["page"].asInt(1);
  page.pageSize = json["pageSize"].asInt(12);
  page.totalPrograms = json["totalPrograms"].asInt(json["total"].asInt(0));
  page.totalPages = json["totalPages"].asInt(1);
  page.hasNextPage = json["hasNextPage"].asBool(page.page < page.totalPages);

  const Json &items = json["programs"].isArray()
    ? json["programs"]
    : (json["epg_listings"].isArray() ? json["epg_listings"] : json["items"]);

  if (items.isArray()) {
    for (const Json &item : items.asArray()) {
      EpgProgram program;
      program.channelId = item["channelId"].asString(item["channel_id"].asString(item["id"].asString("")));
      program.channelName = item["channelName"].asString(item["channel"].asString(""));
      program.title = item["title"].asString(item["name"].asString("Program"));
      program.description = item["description"].asString(item["desc"].asString(""));
      program.start = item["start"].asString(item["start_timestamp"].asString(item["startTime"].asString("")));
      program.stop = item["stop"].asString(item["end"].asString(item["end_timestamp"].asString(item["endTime"].asString(""))));

      if (program.start.empty() && item["start_timestamp"].isNumber()) {
        program.start = std::to_string(item["start_timestamp"].asInt(0));
      }
      if (program.stop.empty() && item["stop_timestamp"].isNumber()) {
        program.stop = std::to_string(item["stop_timestamp"].asInt(0));
      }

      page.programs.push_back(program);
    }
  }

  if (page.totalPrograms == 0) {
    page.totalPrograms = static_cast<int>(page.programs.size());
  }

  return page;
}

} // namespace nstv
