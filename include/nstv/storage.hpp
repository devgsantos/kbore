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
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int page
);

bool saveChannelPage(
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  const ChannelPage &page
);

bool loadChannelPage(
  Provider provider,
  StreamType type,
  const std::string &categoryId,
  int pageNumber,
  ChannelPage &page
);

} // namespace nstv
