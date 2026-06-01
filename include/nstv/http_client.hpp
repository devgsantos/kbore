#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>

namespace nstv {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;
  double totalTimeSeconds = 0.0;
  double downloadedBytes = 0.0;
  std::string contentEncoding;
  bool decodedByCurl = true;
};

using HttpProgressCallback = std::function<void(std::size_t downloadedBytes)>;

class HttpClient {
public:
  HttpResponse postJson(
    const std::string &url,
    const std::string &jsonBody,
    const std::map<std::string, std::string> &headers,
    HttpProgressCallback progress = {},
    bool decodeResponse = true
  ) const;
};

} // namespace nstv
