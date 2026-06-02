#pragma once

#include "nstv/config.hpp"
#include "nstv/http_client.hpp"
#include "nstv/json.hpp"
#include "nstv/models.hpp"
#include <functional>
#include <string>

namespace nstv {

struct ManifestLoadResult {
  Manifest manifest;
};

class ParserApiClient {
public:
  using ProgressCallback = std::function<void(const std::string &message)>;

  explicit ParserApiClient(Config config, ProgressCallback progress = {});

  Manifest loadM3uManifest(const std::string &playlistUrl) const;
  Manifest loadXtreamManifest(const std::string &xtreamUrl) const;
  ManifestLoadResult loadM3uManifestWithCacheText(const std::string &playlistUrl) const;
  ManifestLoadResult loadXtreamManifestWithCacheText(const std::string &xtreamUrl) const;
  ChannelPage loadChannels(
    const std::string &sourceUrl,
    Provider provider,
    StreamType type,
    const std::string &categoryId,
    int page
  ) const;
  NodeChildrenPage loadNodeChildren(
    const std::string &sourceUrl,
    Provider provider,
    StreamType type,
    const std::string &nodeId,
    int page,
    int pageSize = 100
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
  ManifestLoadResult loadManifestEndpointWithCacheText(const std::string &endpoint, const std::string &sourceUrl, Provider provider) const;
  ChannelPage loadChannelsEndpoint(
    const std::string &endpoint,
    const std::string &sourceUrl,
    Provider provider,
    StreamType type,
    const std::string &categoryId,
    int page
  ) const;
  NodeChildrenPage nodeChildrenPageFromJson(const Json &json, StreamType fallbackType) const;

  Manifest manifestFromJson(const Json &json, const std::string &sourceUrl, Provider provider) const;
  MediaNode nodeFromJson(const Json &json, const std::string &fallbackType) const;
  void appendNodeTypes(const MediaNode &node, Manifest &manifest) const;
  ChannelPage channelPageFromJson(const Json &json) const;
  EpgPage epgPageFromJson(const Json &json) const;
  std::string endpoint(const std::string &path) const;
  std::map<std::string, std::string> authHeaders() const;
  std::string requestBody(const std::string &url, const std::string &body) const;
  Json requestJson(const std::string &url, const std::string &body) const;

  void progress(const std::string &message) const;

  Config config_;
  HttpClient http_;
  ProgressCallback progress_;
};

} // namespace nstv
