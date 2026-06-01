#pragma once

#include "nstv/models.hpp"
#include <string>

namespace nstv {

std::string dataDir();
std::string activeManifestPath();
std::string manifestPath(const std::string &playlistId);

bool saveManifest(const Manifest &manifest);
bool loadManifest(Manifest &manifest);
bool loadManifest(const std::string &playlistId, Manifest &manifest);

std::string cacheDir();
std::string channelPageCachePath(
  const std::string &playlistId,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int page
);

bool saveChannelPage(
  const std::string &playlistId,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  const ChannelPage &page
);

bool loadChannelPage(
  const std::string &playlistId,
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int pageNumber,
  ChannelPage &page
);

std::string epgCacheKey(const Channel &channel);
std::string epgCachePath(const std::string &playlistId, const Channel &channel);

bool saveEpgPage(
  const std::string &playlistId,
  const Channel &channel,
  const EpgPage &page
);

bool loadEpgPage(
  const std::string &playlistId,
  const Channel &channel,
  EpgPage &page
);

} // namespace nstv
