#pragma once

#include <CesiumAsync/HttpHeaders.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetRequest.h>
#include <CesiumAsync/IAssetResponse.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace CesiumForUnityNative {

class DiskCacheAssetAccessor
    : public CesiumAsync::IAssetAccessor,
      public std::enable_shared_from_this<DiskCacheAssetAccessor> {
public:
  DiskCacheAssetAccessor(
      const std::shared_ptr<CesiumAsync::IAssetAccessor>& pInnerAccessor,
      const std::string& cacheRootPath);

  virtual CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
  get(const CesiumAsync::AsyncSystem& asyncSystem,
      const std::string& url,
      const std::vector<THeader>& headers = {}) override;

  virtual CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
  request(
      const CesiumAsync::AsyncSystem& asyncSystem,
      const std::string& verb,
      const std::string& url,
      const std::vector<THeader>& headers = std::vector<THeader>(),
      const std::span<const std::byte>& contentPayload = {}) override;

  virtual void tick() noexcept override;

  static bool ClearDiskCache(const std::string& cacheRootPath) noexcept;

private:
  struct CachePaths {
    std::filesystem::path metaPath;
    std::filesystem::path dataPath;
  };

  struct CacheMetadata {
    uint16_t statusCode;
    std::string contentType;
    CesiumAsync::HttpHeaders responseHeaders;
  };

  static std::string computeCacheKey(
      const std::string& verb,
      const std::string& url,
      const std::vector<THeader>& headers);

  CachePaths getPaths(const std::string& key) const;

  bool tryLoadCached(
      const std::string& verb,
      const std::string& url,
      const std::vector<THeader>& requestHeaders,
      const std::string& key,
      std::shared_ptr<CesiumAsync::IAssetRequest>& pRequest) const;

  void storeCached(
      const std::string& key,
      const CesiumAsync::IAssetResponse& response) const;

  static bool writeMetadata(
      const std::filesystem::path& filePath,
      const CacheMetadata& metadata);

  static bool readMetadata(
      const std::filesystem::path& filePath,
      CacheMetadata& metadata);

  static bool writeData(
      const std::filesystem::path& filePath,
      const std::span<const std::byte>& data);

  static bool readData(
      const std::filesystem::path& filePath,
      std::vector<std::byte>& data);

  static bool writeString(std::ofstream& stream, const std::string& value);
  static bool readString(std::ifstream& stream, std::string& value);

  std::shared_ptr<CesiumAsync::IAssetAccessor> _pInnerAccessor;
  std::filesystem::path _cacheRootPath;
  mutable std::mutex _cacheMutex;
};

} // namespace CesiumForUnityNative
