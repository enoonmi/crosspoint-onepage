#include <string>
#include <vector>

#include "AnxPropfindParser.h"
#include "test_harness.h"

using anx::PropfindEntry;
using anx::PropfindParser;

namespace {

// Nextcloud / Apache mod_dav style, D: prefix, Depth: 1 on anx/data/file/.
const char* kNextcloudBody = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:" xmlns:s="http://sabredav.org/ns" xmlns:oc="http://owncloud.org/ns" xmlns:nc="http://nextcloud.org/ns">
 <d:response>
  <d:href>/remote.php/dav/files/alice/anx/data/file/</d:href>
  <d:propstat>
   <d:prop>
    <d:getlastmodified>Wed, 08 Oct 2025 14:02:11 GMT</d:getlastmodified>
    <d:resourcetype><d:collection/></d:resourcetype>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
 </d:response>
 <d:response>
  <d:href>/remote.php/dav/files/alice/anx/data/file/9f2c1ab34d5e6f708192a3b4c5d6e7f8.epub</d:href>
  <d:propstat>
   <d:prop>
    <d:getlastmodified>Wed, 08 Oct 2025 14:02:11 GMT</d:getlastmodified>
    <d:getcontentlength>742113</d:getcontentlength>
    <d:resourcetype/>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
 </d:response>
 <d:response>
  <d:href>/remote.php/dav/files/alice/anx/data/file/%E4%B8%AD%E6%96%87%E4%B9%A6%E5%90%8D.epub</d:href>
  <d:propstat>
   <d:prop>
    <d:getlastmodified>Thu, 09 Oct 2025 01:00:00 GMT</d:getlastmodified>
    <d:getcontentlength>1024</d:getcontentlength>
    <d:resourcetype/>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
 </d:response>
</d:multistatus>
)";

// Generic upstream: uppercase D: prefix with a numeric timezone offset, which
// embedded NAS boxes (Synology / QNAP) commonly emit.
const char* kNasBody = R"(<?xml version="1.0" encoding="utf-8"?>
<D:multistatus xmlns:D="DAV:">
<D:response>
<D:href>/dav/anx/data/file/</D:href>
<D:propstat><D:prop>
<D:getlastmodified>Wed, 08 Oct 2025 22:02:11 +0800</D:getlastmodified>
<D:resourcetype><D:collection/></D:resourcetype>
<D:getcontentlength>0</D:getcontentlength>
</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
</D:response>
<D:response>
<D:href>/dav/anx/data/file/a%20book%20with%20spaces.epub</D:href>
<D:propstat><D:prop>
<D:getlastmodified>Wed, 08 Oct 2025 22:02:11 +0800</D:getlastmodified>
<D:getcontentlength>5321</D:getcontentlength>
<D:resourcetype/>
</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
</D:response>
</D:multistatus>)";

const char* kEmptyBody = R"(<?xml version="1.0"?><d:multistatus xmlns:d="DAV:"></d:multistatus>)";

void testNextcloudBody() {
  anxtest::beginSuite("PropfindParser: Nextcloud body");

  std::vector<PropfindEntry> entries;
  ANX_CHECK_MSG(PropfindParser::parse(kNextcloudBody, entries), "should parse");
  ANX_CHECK_EQ_INT(3, entries.size());

  ANX_CHECK_MSG(entries[0].isCollection, "first entry is the collection");
  ANX_CHECK_EQ_STR("file", entries[0].name);
  ANX_CHECK_MSG(entries[0].hasLastModified, "collection has a timestamp");

  ANX_CHECK_EQ_STR("9f2c1ab34d5e6f708192a3b4c5d6e7f8.epub", entries[1].name);
  ANX_CHECK_MSG(!entries[1].isCollection, "book is not a collection");
  ANX_CHECK_EQ_INT(742113, entries[1].contentLength);
  ANX_CHECK_MSG(entries[1].hasLastModified, "book has a timestamp");
  // 2025-10-08 14:02:11 UTC
  ANX_CHECK_EQ_INT(1759932131LL, entries[1].lastModified);

  // Percent-encoded UTF-8 name must decode to CJK.
  ANX_CHECK_EQ_STR(std::string("\xE4\xB8\xAD\xE6\x96\x87\xE4\xB9\xA6\xE5\x90\x8D.epub"), entries[2].name);
  ANX_CHECK_EQ_INT(1024, entries[2].contentLength);

  anxtest::endSuite("PropfindParser: Nextcloud body");
}

void testNasBody() {
  anxtest::beginSuite("PropfindParser: NAS body with uppercase prefix");

  std::vector<PropfindEntry> entries;
  ANX_CHECK_MSG(PropfindParser::parse(kNasBody, entries), "should parse");
  ANX_CHECK_EQ_INT(2, entries.size());

  ANX_CHECK_MSG(entries[0].isCollection, "collection detected with D: prefix");

  // "+0800" offset: 22:02:11 +08:00 == 14:02:11 UTC
  ANX_CHECK_EQ_INT(1759932131LL, entries[1].lastModified);
  ANX_CHECK_EQ_STR("a book with spaces.epub", entries[1].name);
  ANX_CHECK_EQ_INT(5321, entries[1].contentLength);

  anxtest::endSuite("PropfindParser: NAS body with uppercase prefix");
}

void testEmptyAndMalformed() {
  anxtest::beginSuite("PropfindParser: empty and malformed");

  std::vector<PropfindEntry> entries;
  ANX_CHECK_MSG(PropfindParser::parse(kEmptyBody, entries), "empty multistatus is valid");
  ANX_CHECK_EQ_INT(0, entries.size());

  ANX_CHECK_MSG(!PropfindParser::parse("", entries), "empty body is not a multistatus");
  ANX_CHECK_MSG(!PropfindParser::parse("garbage not xml", entries), "garbage is not a multistatus");
  ANX_CHECK_MSG(!PropfindParser::parse(nullptr, 0, entries), "null body rejected");

  anxtest::endSuite("PropfindParser: empty and malformed");
}

void testSelfClosingAndMissingFields() {
  anxtest::beginSuite("PropfindParser: self-closing and missing fields");

  const char* body = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:">
<d:response>
<d:href>/anx/data/file/minimal.epub</d:href>
<d:propstat><d:prop>
<d:getcontentlength/>
</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>
</d:response>
</d:multistatus>)";

  std::vector<PropfindEntry> entries;
  ANX_CHECK(PropfindParser::parse(body, entries));
  ANX_CHECK_EQ_INT(1, entries.size());
  ANX_CHECK_EQ_STR("minimal.epub", entries[0].name);
  ANX_CHECK_EQ_INT(0, entries[0].contentLength);
  ANX_CHECK_MSG(!entries[0].hasLastModified, "absent timestamp is flagged");
  ANX_CHECK_MSG(!entries[0].isCollection, "absent resourcetype is not a collection");

  anxtest::endSuite("PropfindParser: self-closing and missing fields");
}

void testEntitiesAndTrickyNames() {
  anxtest::beginSuite("PropfindParser: XML entities and tricky names");

  const char* body = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:">
<d:response>
<d:href>/anx/data/file/tom%20%26%20jerry.epub</d:href>
<d:propstat><d:prop><d:getcontentlength>10</d:getcontentlength></d:prop>
<d:status>HTTP/1.1 200 OK</d:status></d:propstat>
</d:response>
<d:response>
<d:href>/anx/data/file/a%2Fb.epub</d:href>
<d:propstat><d:prop><d:getcontentlength>11</d:getcontentlength></d:prop>
<d:status>HTTP/1.1 200 OK</d:status></d:propstat>
</d:response>
<d:response>
<d:href>/anx/data/file/&amp;angle.epub</d:href>
<d:propstat><d:prop><d:getcontentlength>12</d:getcontentlength></d:prop>
<d:status>HTTP/1.1 200 OK</d:status></d:propstat>
</d:response>
</d:multistatus>)";

  std::vector<PropfindEntry> entries;
  ANX_CHECK(PropfindParser::parse(body, entries));
  ANX_CHECK_EQ_INT(3, entries.size());

  ANX_CHECK_EQ_STR("tom & jerry.epub", entries[0].name);
  // Encoded slash stays encoded so it cannot be mistaken for a directory.
  ANX_CHECK_EQ_STR("a%2Fb.epub", entries[1].name);
  ANX_CHECK_EQ_STR("&angle.epub", entries[2].name);

  anxtest::endSuite("PropfindParser: XML entities and tricky names");
}

void testResponseWithoutHrefIsSkipped() {
  anxtest::beginSuite("PropfindParser: response without href");

  const char* body = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:">
<d:response><d:propstat><d:prop><d:getcontentlength>5</d:getcontentlength></d:prop>
<d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response>
<d:response>
<d:href>/anx/data/file/good.epub</d:href>
<d:propstat><d:prop><d:getcontentlength>7</d:getcontentlength></d:prop>
<d:status>HTTP/1.1 200 OK</d:status></d:propstat>
</d:response>
</d:multistatus>)";

  std::vector<PropfindEntry> entries;
  ANX_CHECK(PropfindParser::parse(body, entries));
  ANX_CHECK_EQ_INT(1, entries.size());
  ANX_CHECK_EQ_STR("good.epub", entries[0].name);

  anxtest::endSuite("PropfindParser: response without href");
}

void testHelpers() {
  anxtest::beginSuite("PropfindParser: percentDecode / lastSegment");

  ANX_CHECK_EQ_STR("plain.epub", PropfindParser::percentDecode("plain.epub"));
  ANX_CHECK_EQ_STR("a b.epub", PropfindParser::percentDecode("a%20b.epub"));
  // Default keeps %2F encoded.
  ANX_CHECK_EQ_STR("a%2Fb", PropfindParser::percentDecode("a%2Fb"));
  // Opt-in decodes it.
  ANX_CHECK_EQ_STR("a/b", PropfindParser::percentDecode("a%2Fb", true));
  // A stray '%' that is not a valid escape is passed through untouched.
  ANX_CHECK_EQ_STR("100%", PropfindParser::percentDecode("100%"));
  ANX_CHECK_EQ_STR("%zz", PropfindParser::percentDecode("%zz"));

  ANX_CHECK_EQ_STR("c.epub", PropfindParser::lastSegment("/a/b/c.epub"));
  ANX_CHECK_EQ_STR("b", PropfindParser::lastSegment("/a/b/"));
  ANX_CHECK_EQ_STR("b", PropfindParser::lastSegment("/a/b"));
  ANX_CHECK_EQ_STR("", PropfindParser::lastSegment("/"));
  ANX_CHECK_EQ_STR("", PropfindParser::lastSegment(""));
  ANX_CHECK_EQ_STR("no slash.epub", PropfindParser::lastSegment("no slash.epub"));

  anxtest::endSuite("PropfindParser: percentDecode / lastSegment");
}

}  // namespace

void runPropfindTests() {
  testNextcloudBody();
  testNasBody();
  testEmptyAndMalformed();
  testSelfClosingAndMissingFields();
  testEntitiesAndTrickyNames();
  testResponseWithoutHrefIsSkipped();
  testHelpers();
}
