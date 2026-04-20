#include "DiskCacheAssetAccessor.h"

#include <CesiumAsync/AsyncSystem.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace CesiumForUnityNative {

namespace {

class CachedAssetResponse final : public CesiumAsync::IAssetResponse {
public:
  CachedAssetResponse(
      uint16_t statusCode,
      std::string&& contentType,
      CesiumAsync::HttpHeaders&& headers,
      std::vector<std::byte>&& data)
      : _statusCode(statusCode),
        _contentType(std::move(contentType)),
        _headers(std::move(headers)),
        _data(std::move(data)) {}

  virtual uint16_t statusCode() const override { return this->_statusCode; }

  virtual std::string contentType() const override { return this->_contentType; }

  virtual const CesiumAsync::HttpHeaders& headers() const override {
    return this->_headers;
  }

  virtual std::span<const std::byte> data() const override {
    return this->_data;
  }

private:
  uint16_t _statusCode;
  std::string _contentType;
  CesiumAsync::HttpHeaders _headers;
  std::vector<std::byte> _data;
};

class CachedAssetRequest final : public CesiumAsync::IAssetRequest {
public:
  CachedAssetRequest(
      std::string&& method,
      std::string&& url,
      CesiumAsync::HttpHeaders&& requestHeaders,
      CachedAssetResponse&& response)
      : _method(std::move(method)),
        _url(std::move(url)),
        _requestHeaders(std::move(requestHeaders)),
        _response(std::move(response)) {}

  virtual const std::string& method() const override { return this->_method; }

  virtual const std::string& url() const override { return this->_url; }

  virtual const CesiumAsync::HttpHeaders& headers() const override {
    return this->_requestHeaders;
  }

  virtual const CesiumAsync::IAssetResponse* response() const override {
    return &this->_response;
  }

private:
  std::string _method;
  std::string _url;
  CesiumAsync::HttpHeaders _requestHeaders;
  CachedAssetResponse _response;
};

constexpr uint32_t metadataMagic = 0x31434355;
constexpr uint32_t metadataVersion = 1;

inline uint64_t fnv1aStep(uint64_t hash, uint8_t value) {
  return (hash ^ value) * 1099511628211ull;
}

inline uint64_t fnv1a(const std::string& value, uint64_t hash) {
  for (char c : value) {
    hash = fnv1aStep(hash, static_cast<uint8_t>(c));
  }

  return hash;
}

std::string toHex(uint64_t value) {
  constexpr std::array<char, 16> chars = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  std::string result;
  result.resize(16);

  for (int i = 15; i >= 0; --i) {
    result[size_t(i)] = chars[static_cast<size_t>(value & 0xF)];
    value >>= 4;
  }

  return result;
}

std::string toLowerAscii(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool isCredentialParameterName(const std::string& name) {
  const std::string lower = toLowerAscii(name);
  return lower == "session" || lower == "key" || lower == "token" ||
         lower == "access_token" || lower == "api_key" ||
         lower == "x-goog-api-key";
}

std::string getBaseUrl(const std::string& url) {
  const size_t queryStart = url.find('?');
  if (queryStart == std::string::npos) {
    return url;
  }

  return url.substr(0, queryStart);
}

std::vector<std::pair<std::string, std::string>>
parseQueryParameters(const std::string& url) {
  std::vector<std::pair<std::string, std::string>> result;

  const size_t queryStart = url.find('?');
  if (queryStart == std::string::npos || queryStart + 1 >= url.size()) {
    return result;
  }

  const std::string_view query(
      url.data() + queryStart + 1,
      url.size() - queryStart - 1);
  size_t segmentStart = 0;
  while (segmentStart < query.size()) {
    size_t segmentEnd = query.find('&', segmentStart);
    if (segmentEnd == std::string_view::npos) {
      segmentEnd = query.size();
    }

    std::string_view segment =
        query.substr(segmentStart, segmentEnd - segmentStart);
    if (!segment.empty()) {
      size_t equals = segment.find('=');
      if (equals == std::string_view::npos) {
        result.emplace_back(std::string(segment), std::string());
      } else {
        result.emplace_back(
            std::string(segment.substr(0, equals)),
            std::string(segment.substr(equals + 1)));
      }
    }

    segmentStart = segmentEnd + 1;
  }

  return result;
}

std::string buildUrlWithQuery(
    const std::string& baseUrl,
    const std::vector<std::pair<std::string, std::string>>& parameters) {
  if (parameters.empty()) {
    return baseUrl;
  }

  std::string result = baseUrl;
  result.push_back('?');
  for (size_t i = 0; i < parameters.size(); ++i) {
    if (i > 0) {
      result.push_back('&');
    }
    result += parameters[i].first;
    if (!parameters[i].second.empty()) {
      result.push_back('=');
      result += parameters[i].second;
    }
  }

  return result;
}

std::string normalizeUrlForCaching(const std::string& url) {
  std::vector<std::pair<std::string, std::string>> query =
      parseQueryParameters(url);

  query.erase(
      std::remove_if(
          query.begin(),
          query.end(),
          [](const auto& entry) {
            return isCredentialParameterName(entry.first);
          }),
      query.end());

  std::sort(query.begin(), query.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.first == rhs.first) {
      return lhs.second < rhs.second;
    }
    return lhs.first < rhs.first;
  });

  return buildUrlWithQuery(getBaseUrl(url), query);
}

} // namespace

DiskCacheAssetAccessor::DiskCacheAssetAccessor(
    const std::shared_ptr<CesiumAsync::IAssetAccessor>& pInnerAccessor,
    const std::string& cacheRootPath)
    : _pInnerAccessor(pInnerAccessor), _cacheRootPath(cacheRootPath) {
  std::error_code ec;
  std::filesystem::create_directories(this->_cacheRootPath, ec);
}

CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
DiskCacheAssetAccessor::get(
    const CesiumAsync::AsyncSystem& asyncSystem,
    const std::string& url,
    const std::vector<THeader>& headers) {
  return this->request(asyncSystem, "GET", url, headers, {});
}

CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
DiskCacheAssetAccessor::request(
    const CesiumAsync::AsyncSystem& asyncSystem,
    const std::string& verb,
    const std::string& url,
    const std::vector<THeader>& headers,
    const std::span<const std::byte>& contentPayload) {
  if (verb != "GET" || !contentPayload.empty()) {
    return this->_pInnerAccessor
        ->request(asyncSystem, verb, url, headers, contentPayload);
  }

  const std::string normalizedUrl = normalizeUrlForCaching(url);
  const std::string key = computeCacheKey(verb, normalizedUrl, headers);

  std::shared_ptr<CesiumAsync::IAssetRequest> pCachedRequest;
  if (this->tryLoadCached(verb, url, headers, key, pCachedRequest)) {
    return asyncSystem.createResolvedFuture(std::move(pCachedRequest));
  }

  std::shared_ptr<DiskCacheAssetAccessor> thiz = this->shared_from_this();
  return this->_pInnerAccessor
      ->request(asyncSystem, verb, url, headers, contentPayload)
      .thenImmediately(
          [thiz, key](std::shared_ptr<CesiumAsync::IAssetRequest>&& pRequest)
              -> std::shared_ptr<CesiumAsync::IAssetRequest> {
            if (!pRequest) {
              return nullptr;
            }

            const CesiumAsync::IAssetResponse* pResponse = pRequest->response();
            if (!pResponse) {
              return pRequest;
            }

            if (pResponse->statusCode() >= 200 && pResponse->statusCode() < 300) {
              thiz->storeCached(key, *pResponse);
            }
            return pRequest;
          });
}

void DiskCacheAssetAccessor::tick() noexcept { this->_pInnerAccessor->tick(); }

/*static*/ bool
DiskCacheAssetAccessor::ClearDiskCache(const std::string& cacheRootPath) noexcept {
  std::error_code ec;
  std::filesystem::remove_all(cacheRootPath, ec);
  if (ec) {
    return false;
  }

  std::filesystem::create_directories(cacheRootPath, ec);
  return !ec;
}

/*static*/ std::string DiskCacheAssetAccessor::computeCacheKey(
    const std::string& verb,
    const std::string& url,
    const std::vector<THeader>& headers) {
  uint64_t hash = 1469598103934665603ull;
  hash = fnv1a(verb, hash);
  hash = fnv1a("\n", hash);
  hash = fnv1a(url, hash);

  std::vector<THeader> sortedHeaders = headers;
  std::sort(
      sortedHeaders.begin(),
      sortedHeaders.end(),
      [](const THeader& lhs, const THeader& rhs) {
        if (lhs.first == rhs.first) {
          return lhs.second < rhs.second;
        }

        return lhs.first < rhs.first;
      });

  for (const THeader& header : sortedHeaders) {
    hash = fnv1a("\n", hash);
    hash = fnv1a(header.first, hash);
    hash = fnv1a(":", hash);
    hash = fnv1a(header.second, hash);
  }

  return toHex(hash);
}

DiskCacheAssetAccessor::CachePaths
DiskCacheAssetAccessor::getPaths(const std::string& key) const {
  std::filesystem::path shardPath =
      this->_cacheRootPath / key.substr(0, 2) / key.substr(2, 2);

  return CachePaths{
      shardPath / (key + ".meta"),
      shardPath / (key + ".bin")};
}

bool DiskCacheAssetAccessor::tryLoadCached(
    const std::string& verb,
    const std::string& url,
    const std::vector<THeader>& requestHeaders,
    const std::string& key,
    std::shared_ptr<CesiumAsync::IAssetRequest>& pRequest) const {
  std::lock_guard<std::mutex> lock(this->_cacheMutex);

  CachePaths paths = this->getPaths(key);
  if (!std::filesystem::exists(paths.metaPath) ||
      !std::filesystem::exists(paths.dataPath)) {
    return false;
  }

  CacheMetadata metadata{};
  std::vector<std::byte> data;
  if (!readMetadata(paths.metaPath, metadata) || !readData(paths.dataPath, data)) {
    return false;
  }

  pRequest = std::make_shared<CachedAssetRequest>(
      std::string(verb),
      std::string(url),
      CesiumAsync::HttpHeaders(requestHeaders.begin(), requestHeaders.end()),
      CachedAssetResponse(
          metadata.statusCode,
          std::move(metadata.contentType),
          std::move(metadata.responseHeaders),
          std::move(data)));

  return true;
}

void DiskCacheAssetAccessor::storeCached(
    const std::string& key,
    const CesiumAsync::IAssetResponse& response) const {
  const std::span<const std::byte> responseData = response.data();
  if (responseData.empty()) {
    return;
  }

  CacheMetadata metadata{
      response.statusCode(),
      response.contentType(),
      response.headers()};

  std::lock_guard<std::mutex> lock(this->_cacheMutex);

  CachePaths paths = this->getPaths(key);
  std::error_code ec;
  std::filesystem::create_directories(paths.metaPath.parent_path(), ec);
  if (ec) {
    return;
  }

  if (!writeMetadata(paths.metaPath, metadata)) {
    return;
  }

  if (!writeData(paths.dataPath, responseData)) {
    return;
  }
}

/*static*/ bool DiskCacheAssetAccessor::writeMetadata(
    const std::filesystem::path& filePath,
    const CacheMetadata& metadata) {
  std::filesystem::path tempPath = filePath;
  tempPath += ".tmp";

  std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }

  uint32_t magic = metadataMagic;
  uint32_t version = metadataVersion;
  uint16_t statusCode = metadata.statusCode;
  uint32_t headerCount = static_cast<uint32_t>(metadata.responseHeaders.size());

  stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
  stream.write(reinterpret_cast<const char*>(&statusCode), sizeof(statusCode));

  if (!writeString(stream, metadata.contentType)) {
    return false;
  }

  stream.write(reinterpret_cast<const char*>(&headerCount), sizeof(headerCount));
  for (const auto& header : metadata.responseHeaders) {
    if (!writeString(stream, header.first) || !writeString(stream, header.second)) {
      return false;
    }
  }

  if (!stream) {
    return false;
  }

  stream.close();

  std::error_code ec;
  std::filesystem::rename(tempPath, filePath, ec);
  if (ec) {
    std::filesystem::remove(filePath, ec);
    ec.clear();
    std::filesystem::rename(tempPath, filePath, ec);
    if (ec) {
      std::filesystem::remove(tempPath, ec);
      return false;
    }
  }

  return true;
}

/*static*/ bool DiskCacheAssetAccessor::readMetadata(
    const std::filesystem::path& filePath,
    CacheMetadata& metadata) {
  std::ifstream stream(filePath, std::ios::binary);
  if (!stream) {
    return false;
  }

  uint32_t magic = 0;
  uint32_t version = 0;
  uint16_t statusCode = 0;

  stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  stream.read(reinterpret_cast<char*>(&version), sizeof(version));
  stream.read(reinterpret_cast<char*>(&statusCode), sizeof(statusCode));

  if (!stream || magic != metadataMagic || version != metadataVersion) {
    return false;
  }

  std::string contentType;
  if (!readString(stream, contentType)) {
    return false;
  }

  uint32_t headerCount = 0;
  stream.read(reinterpret_cast<char*>(&headerCount), sizeof(headerCount));
  if (!stream) {
    return false;
  }

  CesiumAsync::HttpHeaders headers;
  for (uint32_t i = 0; i < headerCount; ++i) {
    std::string key;
    std::string value;
    if (!readString(stream, key) || !readString(stream, value)) {
      return false;
    }

    headers.emplace(std::move(key), std::move(value));
  }

  metadata.statusCode = statusCode;
  metadata.contentType = std::move(contentType);
  metadata.responseHeaders = std::move(headers);
  return true;
}

/*static*/ bool DiskCacheAssetAccessor::writeData(
    const std::filesystem::path& filePath,
    const std::span<const std::byte>& data) {
  std::filesystem::path tempPath = filePath;
  tempPath += ".tmp";

  std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }

  if (!data.empty()) {
    stream.write(
        reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
  }

  if (!stream) {
    return false;
  }

  stream.close();

  std::error_code ec;
  std::filesystem::rename(tempPath, filePath, ec);
  if (ec) {
    std::filesystem::remove(filePath, ec);
    ec.clear();
    std::filesystem::rename(tempPath, filePath, ec);
    if (ec) {
      std::filesystem::remove(tempPath, ec);
      return false;
    }
  }

  return true;
}

/*static*/ bool DiskCacheAssetAccessor::readData(
    const std::filesystem::path& filePath,
    std::vector<std::byte>& data) {
  std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
  if (!stream) {
    return false;
  }

  std::streamsize size = stream.tellg();
  if (size < 0) {
    return false;
  }

  data.resize(static_cast<size_t>(size));
  stream.seekg(0, std::ios::beg);
  if (size > 0) {
    stream.read(reinterpret_cast<char*>(data.data()), size);
  }

  return bool(stream);
}

/*static*/ bool DiskCacheAssetAccessor::writeString(
    std::ofstream& stream,
    const std::string& value) {
  uint32_t size = static_cast<uint32_t>(value.size());
  stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
  if (size > 0) {
    stream.write(value.data(), size);
  }

  return bool(stream);
}

/*static*/ bool DiskCacheAssetAccessor::readString(
    std::ifstream& stream,
    std::string& value) {
  uint32_t size = 0;
  stream.read(reinterpret_cast<char*>(&size), sizeof(size));
  if (!stream) {
    return false;
  }

  value.resize(size);
  if (size > 0) {
    stream.read(value.data(), size);
  }

  return bool(stream);
}

} // namespace CesiumForUnityNative
