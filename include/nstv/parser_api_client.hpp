#pragma once

#include "nstv/config.hpp"
#include "nstv/http_client.hpp"
#include "nstv/json.hpp"
#include "nstv/models.hpp"
#include <string>

namespace nstv {

class ParserApiClient {
public:
  explicit ParserApiClient(Config config);

  Manifest loadM3uManifest(const std::string &playlistUrl) const;
  Manifest loadXtreamManifest(const std::string &xtreamUrl) const;
  ChannelPage loadChannels(
    const std::string &sourceUrl,
    Provider provider,
    StreamType type,
    const std::string &categoryId,
    int page
  ) const;

  EpgPage loadEpgPrograms(
    const std::string &sourceUrl,
    Provider provider,
    const Channel &channel,
    int page = 1,
    int pageSize = 12,
    const std::string &manualEpgUrl = ""
  ) const;

private:
  Manifest loadManifestEndpoint(const std::string &endpoint, const std::string &sourceUrl, Provider provider) const;
  ChannelPage loadChannelsEndpoint(
    const std::string &endpoint,
    const std::string &sourceUrl,
    Provider provider,
    StreamType type,
    const std::string &categoryId,
    int page
  ) const;

  Manifest manifestFromJson(const Json &json, const std::string &sourceUrl, Provider provider) const;
  ChannelPage channelPageFromJson(const Json &json) const;
  EpgPage epgPageFromJson(const Json &json) const;
  std::string endpoint(const std::string &path) const;
  std::map<std::string, std::string> authHeaders() const;
  Json requestJson(const std::string &url, const std::string &body) const;

  Config config_;
  HttpClient http_;
};

} // namespace nstv
