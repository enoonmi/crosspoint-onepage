#pragma once

#include <cstdint>

namespace anx {

/**
 * HTTP / WebDAV date parsing.
 *
 * WebDAV PROPFIND responses carry <D:getlastmodified> in RFC 1123 format, e.g.
 *   Sun, 06 Nov 1994 08:49:37 GMT
 * and on some servers
 *   Sun, 06 Nov 1994 08:49:37 +0000
 *
 * The firmware compares this against the SD file's mtime to decide whether a
 * book needs to be re-downloaded, so a silent wrong answer here means either a
 * permanently stale book or a re-download on every sync. Therefore malformed
 * input must be reported, never coerced to 0.
 *
 * This translation unit is deliberately free of Arduino / ESP-IDF headers so it
 * can be compiled and unit-tested on the host.
 */
class HttpDate {
 public:
  /**
   * Parse an RFC 1123 date into a Unix timestamp (UTC seconds).
   *
   * Accepted forms:
   *   "Sun, 06 Nov 1994 08:49:37 GMT"
   *   "Sun, 06 Nov 1994 08:49:37 +0000"
   *   "Sun, 06 Nov 1994 08:49:37 UTC"
   *   "Sunday, 06-Nov-94 08:49:37 GMT"  (RFC 850, as some servers still send)
   *   "06 Nov 1994 08:49:37 GMT"        (no weekday, seen from a few NAS boxes)
   *
   * @param text   NUL-terminated string; may have leading/trailing whitespace
   * @param outUtc Receives the Unix timestamp on success; untouched on failure
   * @return true on success, false on any parse failure (outUtc untouched)
   */
  static bool parseRfc1123(const char* text, int64_t& outUtc);

  /**
   * Format a Unix timestamp as an RFC 1123 date in GMT.
   * Used when writing progress.json, where a human-readable timestamp makes
   * debugging a mis-synced device far easier than a bare epoch integer.
   *
   * @param utc       Unix timestamp
   * @param out       destination buffer
   * @param outSize   capacity of `out` in bytes (32 is always enough)
   * @return true if the formatted string fit, false otherwise
   */
  static bool formatRfc1123(int64_t utc, char* out, size_t outSize);

  /**
   * Convert a broken-down UTC time to a Unix timestamp.
   * Exposed for tests; `year` is the full year (e.g. 1994).
   * Returns false if the fields are out of range.
   */
  static bool toUnixTime(int year, int month, int day, int hour, int minute, int second, int64_t& outUtc);

 private:
  static bool isLeapYear(int year);
  static int daysInMonth(int year, int month);
};

}  // namespace anx
