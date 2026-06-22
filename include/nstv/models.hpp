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

struct MediaNode {
  std::string id;
  std::string title;
  std::string name;
  std::string type;
  std::string kind;
  std::string url;
  std::string logo;
  std::string group;
  std::string groupId;

  // EPG matching metadata propagated by the parser for node-tree manifests.
  // These fields let the app request EPG for focused live items without
  // depending on the legacy loadedChannels list.
  std::string tvgId;
  std::string tvgName;
  std::string streamId;

  int totalItems = 0;
  int totalChannels = 0;
  int childCount = 0;
  bool hasChildren = false;
  bool playable = false;
  std::vector<MediaNode> children;
};

struct Channel {
  std::string id;
  std::string name;
  std::string url;
  std::string logo;
  std::string group;
  std::string groupId;

  // EPG matching fields returned by the parser when available.
  // M3U/XMLTV usually matches by tvgId/tvgName/name.
  // Xtream short EPG usually matches by streamId.
  std::string tvgId;
  std::string tvgName;
  std::string streamId;

  StreamType type = StreamType::Live;
};

struct EpgProgram {
  std::string channelId;
  std::string channelName;
  std::string title;
  std::string description;
  std::string start;
  std::string stop;
};

struct EpgPage {
  std::vector<EpgProgram> programs;
  int page = 1;
  int pageSize = 20;
  int totalPrograms = 0;
  int totalPages = 1;
  bool hasNextPage = false;
};

struct VodDetails {
  std::string title;
  std::string description;
  std::string posterUrl;
  std::string backdropUrl;
  std::string releaseDate;
  std::string provider;
};

struct Manifest {
  std::string id;
  std::string name;
  std::string source;
  Provider provider = Provider::Local;
  int totalChannels = 0;
  int totalItems = 0;
  std::vector<TypeGroup> types;
  std::vector<MediaNode> nodes;
};

struct ChannelPage {
  std::vector<Channel> channels;
  int page = 1;
  int pageSize = 20;
  int totalChannels = 0;
  int totalPages = 1;
  bool hasNextPage = false;
};

struct NodeChildrenPage {
  std::vector<MediaNode> items;
  int page = 1;
  int pageSize = 100;
  int totalItems = 0;
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
