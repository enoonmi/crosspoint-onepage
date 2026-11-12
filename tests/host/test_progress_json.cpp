#include <string>
#include <vector>

#include "AnxProgressJson.h"
#include "test_harness.h"

using anx::BookProgress;
using anx::ProgressDocument;
using anx::ProgressJson;

namespace {

void testClamp() {
  anxtest::beginSuite("ProgressJson: clampPercentage");

  ANX_CHECK(ProgressJson::clampPercentage(-0.5) == 0.0);
  ANX_CHECK(ProgressJson::clampPercentage(1.7) == 1.0);
  ANX_CHECK(ProgressJson::clampPercentage(0.42) == 0.42);
  ANX_CHECK(ProgressJson::clampPercentage(0.0) == 0.0);
  ANX_CHECK(ProgressJson::clampPercentage(1.0) == 1.0);

  anxtest::endSuite("ProgressJson: clampPercentage");
}

void testRoundTrip() {
  anxtest::beginSuite("ProgressJson: write then parse");

  ProgressDocument doc;
  doc.schema = ProgressJson::kSchemaVersion;
  doc.device = "onepage-c61";
  doc.updatedAt = 1762923600;  // 2025-11-12T05:00:00Z
  doc.books.push_back({"file/9f2c1ab34d5e6f708192a3b4c5d6e7f8.epub", 0.4231, 1762920000});
  doc.books.push_back({"file/second book.epub", 1.0, 1762920100});

  const std::string json = ProgressJson::write(doc);
  ANX_CHECK_MSG(json.find("\"schema\": 1") != std::string::npos, "schema written");
  ANX_CHECK_MSG(json.find("onepage-c61") != std::string::npos, "device written");
  ANX_CHECK_MSG(json.find("0.4231") != std::string::npos, "percentage written");
  // A human-readable timestamp is emitted alongside the numeric one.
  ANX_CHECK_MSG(json.find("2025") != std::string::npos, "RFC1123 stamp written");

  ProgressDocument parsed;
  ANX_CHECK_MSG(ProgressJson::parse(json, parsed), "written JSON must re-parse");
  ANX_CHECK_EQ_INT(1, parsed.schema);
  ANX_CHECK_EQ_STR("onepage-c61", parsed.device);
  ANX_CHECK_EQ_INT(1762923600, parsed.updatedAt);
  ANX_CHECK_EQ_INT(2, parsed.books.size());
  ANX_CHECK_EQ_STR("file/9f2c1ab34d5e6f708192a3b4c5d6e7f8.epub", parsed.books[0].filePath);
  ANX_CHECK_EQ_INT(1762920000, parsed.books[0].updatedAt);
  ANX_CHECK_MSG(parsed.books[0].percentage > 0.4230 && parsed.books[0].percentage < 0.4232,
                "percentage round-trips");

  anxtest::endSuite("ProgressJson: write then parse");
}

void testEmptyDocument() {
  anxtest::beginSuite("ProgressJson: empty document");

  ProgressDocument doc;
  doc.updatedAt = 1762923600;
  const std::string json = ProgressJson::write(doc);

  ProgressDocument parsed;
  ANX_CHECK(ProgressJson::parse(json, parsed));
  ANX_CHECK_EQ_INT(0, parsed.books.size());

  // "{}" is a valid, empty document.
  ANX_CHECK(ProgressJson::parse("{}", parsed));
  ANX_CHECK_EQ_INT(0, parsed.books.size());

  anxtest::endSuite("ProgressJson: empty document");
}

void testParseForeignJson() {
  anxtest::beginSuite("ProgressJson: tolerant parsing");

  // Unknown fields, quoted numbers, RFC 1123 timestamp, camelCase aliases.
  const std::string json = R"({
    "schema": 2,
    "device": "other-device",
    "updated_at": "Thu, 12 Nov 2026 01:00:00 GMT",
    "future_field": {"nested": [1, 2, 3]},
    "books": [
      {"file_path": "file/a.epub", "reading_percentage": "0.75", "updated_at": 1762920000,
       "extra": true}
    ]
  })";

  ProgressDocument parsed;
  ANX_CHECK_MSG(ProgressJson::parse(json, parsed), "foreign JSON with unknown fields parses");
  ANX_CHECK_EQ_INT(2, parsed.schema);
  ANX_CHECK_EQ_STR("other-device", parsed.device);
  ANX_CHECK_EQ_INT(1, parsed.books.size());
  ANX_CHECK_EQ_STR("file/a.epub", parsed.books[0].filePath);
  ANX_CHECK_MSG(parsed.books[0].percentage > 0.7499 && parsed.books[0].percentage < 0.7501,
                "quoted number accepted");

  anxtest::endSuite("ProgressJson: tolerant parsing");
}

void testClampingOnParse() {
  anxtest::beginSuite("ProgressJson: out-of-range values are clamped");

  const std::string json = R"({
    "books": [
      {"file_path": "file/low.epub", "reading_percentage": -3.5},
      {"file_path": "file/high.epub", "reading_percentage": 42.0}
    ]
  })";

  ProgressDocument parsed;
  ANX_CHECK(ProgressJson::parse(json, parsed));
  ANX_CHECK_EQ_INT(2, parsed.books.size());
  ANX_CHECK(parsed.books[0].percentage == 0.0);
  ANX_CHECK(parsed.books[1].percentage == 1.0);

  anxtest::endSuite("ProgressJson: out-of-range values are clamped");
}

void testMalformedRejected() {
  anxtest::beginSuite("ProgressJson: malformed input rejected");

  ProgressDocument parsed;
  ANX_CHECK(!ProgressJson::parse("", parsed));
  ANX_CHECK(!ProgressJson::parse("   ", parsed));
  ANX_CHECK(!ProgressJson::parse("not json", parsed));
  ANX_CHECK(!ProgressJson::parse("[1,2,3]", parsed));
  ANX_CHECK(!ProgressJson::parse("{\"books\": [", parsed));
  ANX_CHECK(!ProgressJson::parse("{\"books\": [{\"file_path\": }]}", parsed));
  // An entry without file_path is not usable and must fail the entry parse.
  ANX_CHECK(!ProgressJson::parse("{\"books\": [{\"reading_percentage\": 0.5}]}", parsed));

  anxtest::endSuite("ProgressJson: malformed input rejected");
}

void testUpsert() {
  anxtest::beginSuite("ProgressJson: upsert");

  ProgressDocument doc;
  BookProgress a{"file/a.epub", 0.10, 1000};
  ANX_CHECK(ProgressJson::upsert(doc, a));
  ANX_CHECK_EQ_INT(1, doc.books.size());
  ANX_CHECK_EQ_INT(1000, doc.books[0].updatedAt);

  // A newer value replaces the old one.
  BookProgress newer{"file/a.epub", 0.60, 2000};
  ANX_CHECK(ProgressJson::upsert(doc, newer));
  ANX_CHECK_EQ_INT(1, doc.books.size());
  ANX_CHECK_EQ_INT(2000, doc.books[0].updatedAt);
  ANX_CHECK_MSG(doc.books[0].percentage > 0.59 && doc.books[0].percentage < 0.61, "newer wins");

  // A stale value must not overwrite it.
  BookProgress stale{"file/a.epub", 0.05, 500};
  ANX_CHECK_MSG(!ProgressJson::upsert(doc, stale), "stale update rejected");
  ANX_CHECK_EQ_INT(2000, doc.books[0].updatedAt);
  ANX_CHECK_MSG(doc.books[0].percentage > 0.59, "position unchanged");

  // An empty file_path is rejected.
  BookProgress invalid{"", 0.5, 3000};
  ANX_CHECK(!ProgressJson::upsert(doc, invalid));

  // find() locates entries.
  const BookProgress* found = ProgressJson::find(doc, "file/a.epub");
  ANX_CHECK(found != nullptr);
  ANX_CHECK(found != nullptr && found->updatedAt == 2000);
  ANX_CHECK(ProgressJson::find(doc, "file/missing.epub") == nullptr);

  anxtest::endSuite("ProgressJson: upsert");
}

}  // namespace

void runProgressJsonTests() {
  testClamp();
  testRoundTrip();
  testEmptyDocument();
  testParseForeignJson();
  testClampingOnParse();
  testMalformedRejected();
  testUpsert();
}
