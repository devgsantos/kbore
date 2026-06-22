#pragma once

#include "nstv/models.hpp"
#include <cstddef>
#include <functional>
#include <string>

namespace nstv {

std::string dataDir();
std::string activeManifestPath();
std::string playlistManifestPath(const std::string &playlistId);

bool saveManifestForPlaylist(const Manifest &manifest, const std::string &playlistId);
bool saveManifestTextForPlaylist(
  const std::string &manifestText,
  const std::string &playlistId,
  const std::function<void(std::size_t written, std::size_t total)> &progress = {}
);

bool saveManifestGzipBytesForPlaylist(
  const std::string &gzipBytes,
  const std::string &playlistId,
  const std::function<void(std::size_t written, std::size_t total)> &progress = {}
);
bool loadManifestForPlaylist(Manifest &manifest, const std::string &playlistId);
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

std::string vodDetailsCacheKey(const Channel &channel);
std::string vodDetailsCachePath(const std::string &playlistId, const Channel &channel);
bool saveVodDetails(const std::string &playlistId, const Channel &channel, const VodDetails &details);
bool loadVodDetails(const std::string &playlistId, const Channel &channel, VodDetails &details);

std::string nodeChildrenPageCachePath(
  const std::string &playlistId,
  const std::string &nodeId,
  int page
);

bool saveNodeChildrenPage(
  const std::string &playlistId,
  const std::string &nodeId,
  const NodeChildrenPage &page
);

bool loadNodeChildrenPage(
  const std::string &playlistId,
  const std::string &nodeId,
  int pageNumber,
  NodeChildrenPage &page
);

std::string favoritesDir();
std::string favoritesPathForPlaylist(const std::string &playlistId);
bool saveFavoritesForPlaylist(const std::string &playlistId, const std::vector<Channel> &favorites);
bool loadFavoritesForPlaylist(const std::string &playlistId, std::vector<Channel> &favorites);

} // namespace nstv
