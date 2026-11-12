#pragma once
// Host stub for src/activities/reader/ProgressFile.h.
//
// The real implementation is crash-safe (temp file + rename on the SD card).
// The IO layer only *reads* progress.bin and writes its own journal, so the
// write path here is inert; the read path in AnxProgressStore opens the file
// through HalStorage, whose stub reports "no file exists".

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len) {
  static_cast<void>(cachePath);
  static_cast<void>(data);
  static_cast<void>(len);
  return false;
}

}  // namespace ProgressFile
