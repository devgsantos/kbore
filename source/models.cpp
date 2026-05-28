#include "nstv/models.hpp"

namespace nstv {

std::string toString(StreamType type) {
  switch (type) {
    case StreamType::Live: return "live";
    case StreamType::Movies: return "movies";
    case StreamType::Series: return "series";
    case StreamType::Radio: return "radio";
    case StreamType::Favorites: return "favorites";
  }
  return "live";
}

StreamType streamTypeFromString(const std::string &value) {
  if (value == "movies") return StreamType::Movies;
  if (value == "series") return StreamType::Series;
  if (value == "radio") return StreamType::Radio;
  if (value == "favorites") return StreamType::Favorites;
  return StreamType::Live;
}

std::string toString(Provider provider) {
  switch (provider) {
    case Provider::M3u: return "m3u";
    case Provider::Xtream: return "xtream";
    case Provider::Local: return "local";
  }
  return "local";
}

Provider providerFromString(const std::string &value) {
  if (value == "m3u") return Provider::M3u;
  if (value == "xtream") return Provider::Xtream;
  return Provider::Local;
}

} // namespace nstv
