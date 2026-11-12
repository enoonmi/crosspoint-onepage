#include <cstdint>
#include <string>

#include "AnxHttpDate.h"
#include "test_harness.h"

using anx::HttpDate;

namespace {

void testCanonicalRfc1123() {
  anxtest::beginSuite("HttpDate: canonical RFC 1123");

  int64_t utc = 0;
  // Reference value from the HTTP spec example. 784111777 = Sun, 06 Nov 1994 08:49:37 GMT
  ANX_CHECK_MSG(HttpDate::parseRfc1123("Sun, 06 Nov 1994 08:49:37 GMT", utc),
                "canonical form should parse");
  ANX_CHECK_EQ_INT(784111777LL, utc);

  anxtest::endSuite("HttpDate: canonical RFC 1123");
}

void testNumericOffsets() {
  anxtest::beginSuite("HttpDate: timezone offsets");

  int64_t utc = 0;
  ANX_CHECK(HttpDate::parseRfc1123("Sun, 06 Nov 1994 08:49:37 +0000", utc));
  ANX_CHECK_EQ_INT(784111777LL, utc);

  // +0100 means the wall clock is one hour ahead of UTC.
  ANX_CHECK(HttpDate::parseRfc1123("Sun, 06 Nov 1994 09:49:37 +0100", utc));
  ANX_CHECK_EQ_INT(784111777LL, utc);

  ANX_CHECK(HttpDate::parseRfc1123("Sun, 06 Nov 1994 03:49:37 -0500", utc));
  ANX_CHECK_EQ_INT(784111777LL, utc);

  // UTC spelled out.
  ANX_CHECK(HttpDate::parseRfc1123("Sun, 06 Nov 1994 08:49:37 UTC", utc));
  ANX_CHECK_EQ_INT(784111777LL, utc);

  anxtest::endSuite("HttpDate: timezone offsets");
}

void testRfc850AndNoWeekday() {
  anxtest::beginSuite("HttpDate: alternates");

  int64_t utc = 0;
  // RFC 850: two-digit year, dashes.
  ANX_CHECK_MSG(HttpDate::parseRfc1123("Sunday, 06-Nov-94 08:49:37 GMT", utc),
                "RFC 850 form should parse");
  ANX_CHECK_EQ_INT(784111777LL, utc);

  // Some NAS boxes omit the weekday entirely.
  ANX_CHECK_MSG(HttpDate::parseRfc1123("06 Nov 1994 08:49:37 GMT", utc),
                "form without weekday should parse");
  ANX_CHECK_EQ_INT(784111777LL, utc);

  // Leading / trailing whitespace is tolerated.
  ANX_CHECK(HttpDate::parseRfc1123("  Sun, 06 Nov 1994 08:49:37 GMT  ", utc));
  ANX_CHECK_EQ_INT(784111777LL, utc);

  anxtest::endSuite("HttpDate: alternates");
}

void testLeapAndEpoch() {
  anxtest::beginSuite("HttpDate: leap years and epoch");

  int64_t utc = 0;
  ANX_CHECK(HttpDate::parseRfc1123("Thu, 01 Jan 1970 00:00:00 GMT", utc));
  ANX_CHECK_EQ_INT(0LL, utc);

  // 2000 is a leap year (divisible by 400), 1900 is not (divisible by 100).
  ANX_CHECK(HttpDate::parseRfc1123("Tue, 29 Feb 2000 12:00:00 GMT", utc));
  ANX_CHECK_EQ_INT(951825600LL, utc);

  // Feb 29 1900 does not exist.
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Thu, 29 Feb 1900 12:00:00 GMT", utc),
                "1900-02-29 must be rejected");

  // 2024-02-29 (leap year).
  ANX_CHECK(HttpDate::parseRfc1123("Thu, 29 Feb 2024 00:00:00 GMT", utc));
  ANX_CHECK_EQ_INT(1709164800LL, utc);

  anxtest::endSuite("HttpDate: leap years and epoch");
}

void testRejectsGarbage() {
  anxtest::beginSuite("HttpDate: rejects malformed input");

  int64_t utc = 123456789;
  const int64_t untouched = utc;

  ANX_CHECK_MSG(!HttpDate::parseRfc1123(nullptr, utc), "nullptr must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("", utc), "empty string must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("   ", utc), "whitespace-only must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("not a date at all", utc), "garbage must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Sun, 06 Xxx 1994 08:49:37 GMT", utc),
                "unknown month must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Sun, 32 Nov 1994 08:49:37 GMT", utc),
                "day 32 must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Sun, 06 Nov 1994 25:49:37 GMT", utc),
                "hour 25 must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Sun, 06 Nov 1994 08:49:37", utc),
                "missing timezone must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Sun, 06 Nov 1994 08:49:37 XYZ", utc),
                "unknown timezone must fail");
  ANX_CHECK_MSG(!HttpDate::parseRfc1123("Sun, 06 Nov 1994 08:49:37 +9999", utc),
                "invalid offset must fail");

  // A failed parse must not clobber the caller's value.
  ANX_CHECK_EQ_INT(untouched, utc);

  anxtest::endSuite("HttpDate: rejects malformed input");
}

void testFormatRoundTrip() {
  anxtest::beginSuite("HttpDate: format and round-trip");

  char buffer[40];
  ANX_CHECK(HttpDate::formatRfc1123(784111777LL, buffer, sizeof(buffer)));
  ANX_CHECK_EQ_STR(std::string("Sun, 06 Nov 1994 08:49:37 GMT"), std::string(buffer));

  // Formatting then re-parsing must be lossless for a modern timestamp.
  const int64_t sample = 1762923600LL;  // 2025-11-12T05:00:00Z
  ANX_CHECK(HttpDate::formatRfc1123(sample, buffer, sizeof(buffer)));
  int64_t reparsed = 0;
  ANX_CHECK_MSG(HttpDate::parseRfc1123(buffer, reparsed), "formatted output must re-parse");
  ANX_CHECK_EQ_INT(sample, reparsed);

  // Too-small buffer must fail rather than truncate silently.
  char tiny[8];
  ANX_CHECK(!HttpDate::formatRfc1123(784111777LL, tiny, sizeof(tiny)));

  anxtest::endSuite("HttpDate: format and round-trip");
}

}  // namespace

void runHttpDateTests() {
  testCanonicalRfc1123();
  testNumericOffsets();
  testRfc850AndNoWeekday();
  testLeapAndEpoch();
  testRejectsGarbage();
  testFormatRoundTrip();
}
