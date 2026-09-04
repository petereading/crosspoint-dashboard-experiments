#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <functional>
#include <string>

/**
 * HTTP client utility for fetching content and downloading files. Built on
 * esp_http_client: https is verified against the CA bundle, plain http is
 * used for local servers (transport is chosen from the URL scheme).
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  using CancelCallback = std::function<bool()>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    TIMED_OUT,
  };

  struct DownloadOptions {
    uint32_t operationTimeoutMs = 60000;
    uint32_t overallTimeoutMs = 0;
    bool bypassCache = false;
    CancelCallback cancelRequested;
    // Optional diagnostics. "Image download failed" covers a bad status, a
    // timeout and an SD write failure alike; these tell the caller which.
    int* outHttpStatus = nullptr;
    size_t* outBytesReceived = nullptr;
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "");

  /**
   * Download with request-level timeouts, cache control, and callback-based
   * cancellation. The transfer remains synchronous; callbacks are checked
   * between bounded socket operations.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      const DownloadOptions& options, ProgressCallback progress = nullptr,
                                      const std::string& username = "", const std::string& password = "");
};
