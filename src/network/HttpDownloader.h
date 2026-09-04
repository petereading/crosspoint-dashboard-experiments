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
    // Optional: receives the final HTTP status (after redirects), or 0 if the
    // request never got that far. Lets a caller report "HTTP 500" instead of the
    // undifferentiated HTTP_ERROR when a transfer fails.
    int* outHttpStatus = nullptr;
    // Optional: receives the number of body bytes written before the transfer
    // ended, however it ended. Separates "never got a byte" (DNS, TCP, TLS)
    // from "stalled part-way through the body".
    size_t* outBytesReceived = nullptr;
    // How long the body may go without delivering data before the transfer is
    // called dead. esp_http_client_read() returns 0 for "nothing arrived within
    // operationTimeoutMs", which is not end-of-stream: on a slow link a live
    // download pauses regularly. 0 keeps the original behaviour of failing on
    // the first such pause.
    uint32_t maxStallMs = 0;
    // Optional: receives the longest single sink write (ms). A transfer can
    // stall because the reader stopped reading -- a slow SD write closes the TCP
    // window and the server goes quiet -- which looks identical to a network
    // fault from the outside. This tells the two apart.
    uint32_t* outSlowestWriteMs = nullptr;
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
