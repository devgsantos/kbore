#pragma once

#include <string>
#include <vector>

namespace nstv {

enum class Provider { Local, M3u, Xtream };

enum class StreamType { Live, Movies, Series, Radio, Favorites };

struct Category {
  std::string id;
  std::string name;
  int totalChannels = 0;
  StreamType type = StreamType::Live;
};

struct TypeGroup {
  StreamType id = StreamType::Live;
  std::string label;
  int totalChannels = 0;
  std::vector<Category> categories;
};

struct Channel {
  std::string id;
  std::string name;
  std::string url;
  std::string logo;
  std::string group;
  std::string groupId;
  StreamType type = StreamType::Live;
};

struct Manifest {
  std::string id;
  std::string name;
  std::string source;
  Provider provider = Provider::Local;
  int totalChannels = 0;
  std::vector<TypeGroup> types;
};

struct ChannelPage {
  std::vector<Channel> channels;
  int page = 1;
  int pageSize = 20;
  int totalChannels = 0;
  int totalPages = 1;
  bool hasNextPage = false;
};

struct SavedPlaylist {
  std::string id;
  std::string name;
  std::string source;
  Provider provider = Provider::Local;
};

std::string toString(StreamType type);
StreamType streamTypeFromString(const std::string &value);
std::string toString(Provider provider);
Provider providerFromString(const std::string &value);

} // namespace nstv
