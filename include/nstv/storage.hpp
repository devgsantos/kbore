#pragma once

#include "nstv/models.hpp"
#include <string>

namespace nstv {

std::string dataDir();
std::string activeManifestPath();

bool saveManifest(const Manifest &manifest);
bool loadManifest(Manifest &manifest);

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

} // namespace nstv
