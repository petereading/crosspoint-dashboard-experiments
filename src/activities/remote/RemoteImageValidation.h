#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace RemoteImageValidation {

enum class BmpError : uint8_t {
  Ok = 0,
  HeaderTooShort,
  NotBmp,
  DibTooSmall,
  BadDimensions,
  ImageTooLarge,
  BadPlanes,
  UnsupportedBpp,
  UnsupportedCompression,
  PaletteTooLarge,
  BadPixelOffset,
  TruncatedPixelData,
};

struct BmpInfo {
  int32_t width = 0;
  int32_t height = 0;
  uint16_t bitsPerPixel = 0;
  uint32_t pixelOffset = 0;
  uint64_t requiredFileSize = 0;
};

bool isHttpsUrl(std::string_view url);

// Reduce a configured Dashboard Worker URL to the origin-plus-path the built-in
// cards append their own route to. Trims surrounding whitespace, drops any query
// or fragment, and drops one trailing card route (a final "<name>.bmp" segment)
// so pasting a card URL cannot break the other cards. A Worker deployed on a
// sub-path keeps that prefix: "https://host/crosspoint/clock.bmp" reduces to
// "https://host/crosspoint", not "https://host".
std::string workerBaseUrl(std::string_view url);
BmpError validateBmp(const uint8_t* header, size_t headerSize, uint64_t fileSize, BmpInfo* info = nullptr);
const char* errorToString(BmpError error);

}  // namespace RemoteImageValidation
