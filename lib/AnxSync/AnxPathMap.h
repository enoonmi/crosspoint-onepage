#pragma once

#include <string>

namespace anx {

/**
 * Maps Anx Reader remote paths onto the reader's SD card layout, with the
 * safety checks that keep a hostile or misconfigured server from writing
 * outside the book directory.
 *
 * Anx Reader stores books at `<base>/anx/data/file/<hash>.epub`, where the file
 * name is a truncated MD5 of the book content (see the Anx WebDAV protocol
 * notes in docs/01). The display title lives in `tb_books.title` and in the
 * EPUB's own OPF metadata, never in the file name.
 *
 * All functions are pure string operations: no filesystem access, no Arduino
 * dependency, so they are covered by host tests.
 */
class PathMap {
 public:
  /** Directory (on the SD card) that downloaded books are written to. */
  static const char* booksDir();

  /** Remote subdirectory, relative to the WebDAV base URL, holding books. */
  static const char* remoteBooksDir();

  /** Remote file holding reading progress pushed by this device. */
  static const char* remoteProgressPath();

  /**
   * Decide whether `name` is a book we are willing to download.
   *
   * Rejected: empty names, "." / "..", anything containing a path separator or
   * NUL, names that start with a dot (hidden/partial files), and anything whose
   * extension is not `.epub` (case-insensitive). Anx Reader converts MOBI /
   * AZW3 / FB2 / TXT / PDF to EPUB at import time, so `.epub` is the only book
   * format that legitimately appears under `anx/data/file/`.
   */
  static bool isAcceptableBookName(const std::string& name);

  /**
   * Map a remote book file name to its SD card path.
   *
   * @param name  remote file name, e.g. "9f2c1a....epub"
   * @param out   receives "/Books/9f2c1a....epub"
   * @return false when `name` is not acceptable (see isAcceptableBookName)
   */
  static bool localBookPath(const std::string& name, std::string& out);

  /**
   * Map a remote book file name to the path used for the in-flight download.
   * Downloading to a temporary name and renaming on success means an
   * interrupted sync can never leave a truncated file that the reader would
   * then try to parse.
   */
  static bool localTempBookPath(const std::string& name, std::string& out);

  /**
   * Map a remote book file name to the path that holds the previous copy while a
   * replacement is being put in place.
   *
   * SdFat's rename() does not overwrite, so an existing file has to be moved out
   * of the way first. Deleting it instead would open a window where a failed
   * rename loses the book entirely; moving it to this name keeps a copy that can
   * be restored. The suffix is not `.epub`, so the name rules reject it and it can
   * never be mistaken for a book.
   */
  static bool localBackupBookPath(const std::string& name, std::string& out);

  /**
   * Strip a leading WebDAV base URL (scheme://host/prefix) from an href.
   *
   * Some servers return absolute hrefs ("https://cloud.example.com/dav/anx/...")
   * instead of absolute paths ("/dav/anx/..."), depending on the request and
   * the server. Returns the path portion in both cases.
   */
  static std::string stripBaseUrl(const std::string& href);

  /**
   * True when `path` stays inside `root` after resolving "." / ".." segments.
   * Used as a last line of defence before touching the filesystem.
   */
  static bool isInsideRoot(const std::string& root, const std::string& path);
};

}  // namespace anx
