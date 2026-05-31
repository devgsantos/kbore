#ifdef __SWITCH__
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include <curl/curl.h>
#include "nstv/http_client.hpp"
#include <cstdio>
#include <cctype>
#include <utility>

namespace nstv {

namespace {

struct WriteContext {
  std::string *body = nullptr;
  HttpProgressCallback progress;
  std::size_t lastReportedBytes = 0;
};

struct HeaderContext {
  std::string *contentEncoding = nullptr;
};

static size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *ctx = static_cast<HeaderContext *>(userdata);
  const std::size_t bytes = size * nmemb;

  if (!ctx || !ctx->contentEncoding || !ptr) {
    return bytes;
  }

  std::string header(ptr, bytes);
  const std::string prefix = "content-encoding:";

  std::string lower;
  lower.reserve(header.size());

  for (char ch : header) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }

  const std::size_t pos = lower.find(prefix);

  if (pos != std::string::npos) {
    std::string value = header.substr(pos + prefix.size());

    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.erase(value.begin());
    }

    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
      value.pop_back();
    }

    *ctx->contentEncoding = value;
  }

  return bytes;
}

static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *ctx = static_cast<WriteContext *>(userdata);
  const std::size_t bytes = size * nmemb;

  if (!ctx || !ctx->body) {
    return 0;
  }

  ctx->body->append(ptr, bytes);

  if (ctx->progress) {
    const std::size_t current = ctx->body->size();

    if (current == 0 || current - ctx->lastReportedBytes >= 256 * 1024) {
      ctx->lastReportedBytes = current;
      ctx->progress(current);
    }
  }

  return bytes;
}

} // namespace

HttpResponse HttpClient::postJson(
  const std::string &url,
  const std::string &jsonBody,
  const std::map<std::string, std::string> &headers,
  HttpProgressCallback progress,
  bool decodeResponse
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
  headerList = curl_slist_append(headerList, "User-Agent: kboré-Switch/0.1");

  if (!decodeResponse) {
    headerList = curl_slist_append(headerList, "Accept-Encoding: gzip");
  }

  for (const auto &[key, value] : headers) {
    std::string item = key + ": " + value;
    headerList = curl_slist_append(headerList, item.c_str());
  }

  WriteContext writeContext;
  writeContext.body = &result.body;
  writeContext.progress = std::move(progress);

  HeaderContext headerContext;
  headerContext.contentEncoding = &result.contentEncoding;
  result.decodedByCurl = decodeResponse;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeContext);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerContext);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  /*
    Normal small API calls can be decoded automatically by libcurl.

    For huge manifest downloads, ParserApiClient asks for the raw gzip body
    instead. That lets kboré save the compressed API response directly to SD
    without recompressing tens of MB on the Switch.
  */
  if (decodeResponse) {
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  }

  /*
    Large dynamic manifests can take a while on the Switch, but we still do
    not want the UI worker to wait forever if the transfer stalls.
  */
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 128L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &result.totalTimeSeconds);
  curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &result.downloadedBytes);

  if (writeContext.progress) {
    writeContext.progress(result.body.size());
  }

  if (code != CURLE_OK) {
    result.error = curl_easy_strerror(code);
  }

  std::printf(
    "[KBORE][HTTP] status=%ld body=%zu bytes downloaded=%.0f bytes time=%.2fs encoding=%s decoded=%d url=%s\n",
    result.status,
    result.body.size(),
    result.downloadedBytes,
    result.totalTimeSeconds,
    result.contentEncoding.c_str(),
    result.decodedByCurl ? 1 : 0,
    url.c_str()
  );

  curl_slist_free_all(headerList);
  curl_easy_cleanup(curl);
  return result;
}

} // namespace nstv
