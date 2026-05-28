#pragma once

#include <map>
#include <string>

namespace nstv {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;
};

class HttpClient {
public:
  HttpResponse postJson(
    const std::string &url,
    const std::string &jsonBody,
    const std::map<std::string, std::string> &headers
  ) const;
};

} // namespace nstv
