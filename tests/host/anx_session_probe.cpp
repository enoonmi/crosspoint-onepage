#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Session probe: runs one complete Anx Sync pass against a live WebDAV server.
//
// The other host programs check single modules. This one performs the sequence
// the device actually performs, using the same code that ships:
//
//     PROPFIND /anx/data/file/  ->  AnxPropfindParser  ->  buildSyncPlan
//     GET each chosen book      ->  byte count vs Content-Length
//     GET  /anx/onepage/        ->  read the previous progress document
//     PUT  /anx/onepage/progress.json
//     GET  it back              ->  the upload parses
//
// It deliberately does not print "OK": tools/e2e_session.py asserts on the
// server's end state, so a probe that lied about itself would still be caught.
// The exit code only reports whether the mechanics completed.
//
// Networking is plain HTTP over Winsock, not esp_http_client. That is a real
// limitation, not an oversight: the point is to exercise the parsing, planning,
// path-building and document-writing logic against a real server. TLS and the
// ESP-IDF client are what the CI build covers.
//
// Usage:
//   anx_session_probe --port N --user U --password P [--host H]

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

#include "AnxPathMap.h"
#include "AnxProgressBridge.h"
#include "AnxProgressJson.h"
#include "AnxPropfindParser.h"
#include "AnxSyncPlan.h"

namespace {

struct Args {
  int port = 0;
  std::string user;
  std::string password;
  std::string host = "127.0.0.1";
};

void closeSocket(socket_t sock) {
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

std::string base64(const std::string& input) {
  static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int value = 0;
  int bits = -6;
  for (unsigned char c : input) {
    value = (value << 8) + c;
    bits += 8;
    while (bits >= 0) {
      out.push_back(table[(value >> bits) & 0x3F]);
      bits -= 6;
    }
  }
  if (bits > -6) out.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

struct Response {
  int status = 0;
  std::string body;
  long long contentLength = -1;
};

// A deliberately small HTTP/1.1 client: this probe needs one request per
// connection, which keeps it readable and makes a server misbehaviour obvious.
bool httpRequest(const Args& args, const std::string& method, const std::string& path,
                 const std::string& extraHeaders, const std::string& body, Response& out) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* resolved = nullptr;
  const std::string portText = std::to_string(args.port);
  if (getaddrinfo(args.host.c_str(), portText.c_str(), &hints, &resolved) != 0) {
    return false;
  }

  socket_t sock = INVALID_SOCKET;
  for (addrinfo* it = resolved; it != nullptr; it = it->ai_next) {
    sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (sock == INVALID_SOCKET) continue;
    if (connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) break;
    closeSocket(sock);
    sock = INVALID_SOCKET;
  }
  freeaddrinfo(resolved);
  if (sock == INVALID_SOCKET) return false;

  std::string request;
  request += method + " " + path + " HTTP/1.1\r\n";
  request += "Host: " + args.host + ":" + portText + "\r\n";
  request += "Authorization: Basic " + base64(args.user + ":" + args.password) + "\r\n";
  request += "Connection: close\r\n";
  if (!extraHeaders.empty()) request += extraHeaders;
  if (!body.empty()) request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  request += "\r\n";
  request += body;

  size_t sent = 0;
  while (sent < request.size()) {
    const int written =
        send(sock, request.data() + sent, static_cast<int>(request.size() - sent), 0);
    if (written <= 0) {
      closeSocket(sock);
      return false;
    }
    sent += static_cast<size_t>(written);
  }

  std::string raw;
  char buffer[8192];
  for (;;) {
    const int got = recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0);
    if (got <= 0) break;
    raw.append(buffer, static_cast<size_t>(got));
  }
  closeSocket(sock);

  const size_t headerEnd = raw.find("\r\n\r\n");
  if (headerEnd == std::string::npos) return false;

  const std::string headers = raw.substr(0, headerEnd);
  out.body = raw.substr(headerEnd + 4);

  const int firstSpace = static_cast<int>(headers.find(' '));
  if (firstSpace < 0) return false;
  out.status = std::atoi(headers.c_str() + firstSpace + 1);

  // Content-Length, needed because the mock closes the socket after the body but
  // a real server might use chunked encoding.
  std::string lower = headers;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::string needle = "content-length:";
  const size_t at = lower.find(needle);
  if (at != std::string::npos) {
    out.contentLength = std::atoll(headers.c_str() + at + needle.size());
  }
  return true;
}

bool parseArgs(int argc, char** argv, Args& args) {
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const bool hasValue = (i + 1 < argc);
    if (flag == "--port" && hasValue) {
      args.port = std::atoi(argv[++i]);
    } else if (flag == "--user" && hasValue) {
      args.user = argv[++i];
    } else if (flag == "--password" && hasValue) {
      args.password = argv[++i];
    } else if (flag == "--host" && hasValue) {
      args.host = argv[++i];
    } else {
      return false;
    }
  }
  return args.port > 0;
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) {
    std::fprintf(stderr, "usage: anx_session_probe --port N --user U --password P\n");
    return 2;
  }

#ifdef _WIN32
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::fprintf(stderr, "WSAStartup failed\n");
    return 3;
  }
#endif

  int failures = 0;
  const auto fail = [&](const std::string& why) {
    std::fprintf(stderr, "  FAIL: %s\n", why.c_str());
    ++failures;
  };

  // --- 1. list the remote book directory ---
  Response listing;
  if (!httpRequest(args, "PROPFIND", "/anx/data/file/", "Depth: 1\r\n", "", listing)) {
    fail("PROPFIND /anx/data/file/ did not complete");
    return 1;
  }
  if (listing.status != 207) {
    fail("PROPFIND expected 207, got " + std::to_string(listing.status));
    return 1;
  }
  std::printf("  PROPFIND: %d, %zu bytes\n", listing.status, listing.body.size());

  std::vector<anx::PropfindEntry> entries;
  if (!anx::PropfindParser::parse(listing.body, entries)) {
    fail("AnxPropfindParser rejected the live multistatus body");
    return 1;
  }

  // Convert the parsed entries into the planner's RemoteFile shape.
  std::vector<anx::RemoteFile> remote;
  for (const auto& entry : entries) {
    if (entry.isCollection) continue;  // the directory itself
    if (entry.name.empty()) continue;
    anx::RemoteFile file;
    file.name = entry.name;
    file.size = entry.contentLength;
    file.lastModified = entry.lastModified;
    file.hasLastModified = entry.hasLastModified;
    remote.push_back(file);
  }
  std::printf("  parsed %zu remote book entries\n", remote.size());

  // --- 2. plan a download: no local books yet, so every remote book is new ---
  std::vector<anx::LocalFile> local;  // empty: a fresh device
  const anx::SyncPlan plan = anx::buildSyncPlan(remote, local);
  const int toDownload = plan.countOf(anx::SyncAction::Download);
  const int toSkip = plan.countOf(anx::SyncAction::Skip);
  // Rejected names are collected in plan.ignored, not as SyncAction::Ignore
  // items - countOf(Ignore) is always 0 for them. Counting the vector is what
  // reports the real number.
  const int ignored = static_cast<int>(plan.ignored.size());
  std::printf("  plan: %d to download, %d to skip, %d ignored\n", toDownload, toSkip, ignored);

  if (toDownload == 0) {
    fail("plan produced no downloads for a fresh device with remote books");
  }

  std::vector<anx::SyncPlanItem> downloads;
  for (const auto& item : plan.items) {
    if (item.action == anx::SyncAction::Download) downloads.push_back(item);
  }

  // --- 3. download each planned book and verify the byte count ---
  for (const auto& item : downloads) {
    Response download;
    const std::string path = "/anx/data/file/" + item.remoteName;
    if (!httpRequest(args, "GET", path, "", "", download)) {
      fail("GET " + path + " did not complete");
      continue;
    }
    if (download.status != 200) {
      fail("GET " + path + " returned " + std::to_string(download.status));
      continue;
    }
    if (download.contentLength >= 0 &&
        download.contentLength != static_cast<long long>(download.body.size())) {
      fail("GET " + path + ": body " + std::to_string(download.body.size()) +
           " bytes but Content-Length said " + std::to_string(download.contentLength));
      continue;
    }
    if (item.localPath.empty()) {
      fail("planner produced an empty local path for " + item.remoteName);
    }
    std::printf("    %s -> %s (%zu bytes)\n", path.c_str(), item.localPath.c_str(),
                download.body.size());
  }

  // --- 4. read the previous progress document ---
  Response previous;
  if (httpRequest(args, "GET", "/anx/onepage/progress.json", "", "", previous) &&
      previous.status == 200) {
    anx::ProgressDocument existing;
    if (anx::ProgressJson::parse(previous.body, existing)) {
      std::printf("  previous progress.json: %zu entries, schema %d\n", existing.books.size(),
                  existing.schema);
    } else {
      fail("AnxProgressJson could not parse the server's progress.json");
    }
  } else {
    std::printf("  no previous progress.json on the server\n");
  }

  // --- 5. build and PUT this device's progress document ---
  std::vector<anx::BookProgress> deviceState;
  const std::vector<uint32_t> cumulative = {100, 200, 300};
  int index = 0;
  for (const auto& item : downloads) {
    const anx::ProgressBridge::DevicePosition position{index % 3, 1 + (index * 3) % 11, 12};
    anx::BookProgress progress;
    // file_path is "file/<name>", the key Anx Reader joins on. Not a local path
    // and not a full remote path.
    progress.filePath = "file/" + item.remoteName;
    progress.percentage = anx::ProgressBridge::positionToPercentage(cumulative, position);
    progress.updatedAt = 1762924000 + index;
    deviceState.push_back(progress);
    ++index;
  }

  anx::ProgressDocument phone;
  phone.schema = anx::ProgressJson::kSchemaVersion;
  phone.device = "phone";
  phone.updatedAt = 1762900000;

  const anx::ProgressDocument upload = anx::ProgressBridge::buildUploadDocument(
      deviceState, phone, anx::ProgressJson::defaultDeviceName(), 1762924000);
  const std::string document = anx::ProgressJson::write(upload);

  Response put;
  if (!httpRequest(args, "PUT", "/anx/onepage/progress.json",
                   "Content-Type: application/json\r\n", document, put)) {
    fail("PUT progress.json did not complete");
  } else if (put.status != 200 && put.status != 201 && put.status != 204) {
    fail("PUT progress.json returned " + std::to_string(put.status));
  } else {
    std::printf("  PUT progress.json: %d, %zu bytes uploaded\n", put.status, document.size());
  }

  // --- 6. read it back: the server must have stored what we sent ---
  Response readBack;
  if (!httpRequest(args, "GET", "/anx/onepage/progress.json", "", "", readBack) ||
      readBack.status != 200) {
    fail("progress.json is not readable after PUT");
  } else {
    anx::ProgressDocument reparsed;
    if (!anx::ProgressJson::parse(readBack.body, reparsed)) {
      fail("the document we uploaded does not parse back");
    } else if (reparsed.books.size() != upload.books.size()) {
      fail("round trip changed the entry count: " + std::to_string(upload.books.size()) + " -> " +
           std::to_string(reparsed.books.size()));
    } else {
      std::printf("  read back: %zu entries, schema %d\n", reparsed.books.size(),
                  reparsed.schema);
    }
  }

  // --- 7. the safety contract, asserted from the device side too ---
  for (const auto& item : downloads) {
    if (!anx::PathMap::isAcceptableBookName(item.remoteName)) {
      fail("plan named a book that fails the name rules: " + item.remoteName);
    }
  }
  if (anx::PathMap::remoteProgressPath() != std::string("anx/onepage/progress.json")) {
    fail("PathMap::remoteProgressPath() changed");
  }

#ifdef _WIN32
  WSACleanup();
#endif

  if (failures > 0) {
    std::fprintf(stderr, "  %d failure(s)\n", failures);
    return 1;
  }
  std::printf("  session mechanics completed\n");
  return 0;
}
