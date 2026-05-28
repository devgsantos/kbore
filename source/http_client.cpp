#ifdef __SWITCH__
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include <curl/curl.h>
#include "nstv/http_client.hpp"

namespace nstv {

static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *body = static_cast<std::string *>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}

HttpResponse HttpClient::postJson(
  const std::string &url,
  const std::string &jsonBody,
  const std::map<std::string, std::string> &headers
) const {
  HttpResponse result;

  CURL *curl = curl_easy_init();
  if (!curl) {
    result.error = "Failed to initialize curl";
    return result;
  }

  struct curl_slist *headerList = nullptr;
  headerList = curl_slist_append(headerList, "Content-Type: application/json");
  headerList = curl_slist_append(headerList, "Accept: application/json, text/plain, */*");
  headerList = curl_slist_append(headerList, "User-Agent: NSTV-Switch/0.1");

  for (const auto &[key, value] : headers) {
    std::string item = key + ": " + value;
    headerList = curl_slist_append(headerList, item.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

  CURLcode code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);

  if (code != CURLE_OK) {
    result.error = curl_easy_strerror(code);
  }

  curl_slist_free_all(headerList);
  curl_easy_cleanup(curl);
  return result;
}

} // namespace nstv
