#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anx {

/** One entry from a WebDAV multistatus response. */
struct PropfindEntry {
  /** Path as returned by the server, e.g. "/remote.php/dav/files/u/anx/data/file/" */
  std::string href;
  /** Last segment of `href` after percent-decoding, e.g. "abc123.epub". */
  std::string name;
  /** True when <D:resourcetype> contains <D:collection/>. */
  bool isCollection = false;
  /** Size in bytes from <D:getcontentlength>; 0 when absent. */
  int64_t contentLength = 0;
  /** Parsed <D:getlastmodified> as a Unix timestamp; 0 when absent/unparseable. */
  int64_t lastModified = 0;
  /** True when <D:getlastmodified> was present and parsed successfully. */
  bool hasLastModified = false;
};

/**
 * Parser for the `207 Multi-Status` XML body returned by WebDAV PROPFIND.
 *
 * Design notes
 * ------------
 * The firmware already vendors expat (`lib/expat`) and uses it for EPUB OPF,
 * but PROPFIND bodies are small and flat, and this module must stay host
 * testable. A hand-written scanner over the response is used instead: it is a
 * few hundred bytes of code, has no allocation surprises, and lets us be
 * deliberately lenient about the things servers disagree on.
 *
 * Things real servers do that this parser tolerates:
 *   - Namespace prefixes are arbitrary: `D:`, `d:`, `lp1:`, or none at all.
 *   - Elements may be self-closing (`<D:getcontentlength/>`).
 *   - `getlastmodified` may be GMT, UTC, or a numeric offset.
 *   - The first entry is usually the collection itself (Depth: 1); callers
 *     filter it out via `isCollection`.
 *   - `href` is percent-encoded, sometimes with the whole path encoded
 *     including slashes (%2F). Decoding must therefore decode per segment,
 *     not wholesale, or `/a/b` and `/a%2Fb` become indistinguishable.
 *   - Some servers omit `getcontentlength` for collections entirely.
 *
 * Rejected as malformed (returns false) rather than silently mis-parsed:
 *   - Body that does not look like a multistatus document at all.
 *   - Entries with an empty href.
 */
class PropfindParser {
 public:
  /**
   * Parse a multistatus body.
   *
   * @param xml     body bytes (need not be NUL-terminated)
   * @param length  body length in bytes
   * @param out     receives the entries in document order; cleared first
   * @return true if the document looked like a multistatus response and at
   *         least one `<response>` element was seen; false otherwise.
   *         A well-formed multistatus with zero responses returns true and
   *         leaves `out` empty.
   */
  static bool parse(const char* xml, size_t length, std::vector<PropfindEntry>& out);

  /** Convenience overload for NUL-terminated bodies. */
  static bool parse(const std::string& xml, std::vector<PropfindEntry>& out);

  /**
   * Percent-decode a single path segment or a whole href held as one string.
   *
   * `%2F` is decoded to '/' only when `decodeSlashes` is true; the default is
   * false so that a filename containing an encoded slash does not silently
   * become a directory separator.
   */
  static std::string percentDecode(const std::string& in, bool decodeSlashes = false);

  /**
   * Return the last non-empty segment of an href, percent-decoded.
   * "/a/b/c.epub" -> "c.epub"; "/a/b/" -> "b".
   */
  static std::string lastSegment(const std::string& href);
};

}  // namespace anx
