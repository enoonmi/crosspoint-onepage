#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "AnxProgressBridge.h"
#include "AnxProgressJson.h"
#include "AnxProgressLocator.h"
#include "AnxPropfindParser.h"
#include "AnxSyncPlan.h"

// Contract probe: drives the firmware's progress pipeline with fixed inputs and
// prints the document it would upload.
//
// The point is cross-language verification. The output of this program is fed to
// tools/verify_contract.py, which parses it with the *same logic the phone uses*
// (see anx-reader/lib/service/sync/onepage_progress_merge.dart). If both sides
// agree on the path, the schema, the field names and the timestamp semantics,
// then the two implementations really can talk to each other.
//
// Without this, "the two sides agree" would only be an assertion in a document.
//
// Usage:
//     anx_contract_probe            # prints the document to stdout
//     anx_contract_probe --json     # machine-readable wrapper

namespace {

// A three-chapter book, matching the shape Anx Reader stores in tb_books:
// 100 + 100 + 100 decoded bytes, chapter page counts as laid out by the reader.
const std::vector<uint32_t> kCumulative = {100, 200, 300};

}  // namespace

int main(int argc, char** argv) {
  const bool jsonMode = (argc > 1 && std::strcmp(argv[1], "--json") == 0);

  // Build a device-side view of a few books, the way AnxProgressStore would
  // after the user has read them.
  std::vector<anx::BookProgress> device;

  // 1. Mid-chapter-1 position, the common case.
  {
    const anx::ProgressBridge::DevicePosition pos{1, 15, 30};
    const double pct = anx::ProgressBridge::positionToPercentage(kCumulative, pos);
    device.push_back({"file/aaaa1111bbbb2222cccc3333dddd4444.epub", pct, 1762923600});
  }

  // 2. Start of the book.
  {
    const anx::ProgressBridge::DevicePosition pos{0, 0, 12};
    const double pct = anx::ProgressBridge::positionToPercentage(kCumulative, pos);
    device.push_back({"file/eeee5555ffff6666aaaa7777bbbb8888.epub", pct, 1762923700});
  }

  // 3. A position that lands exactly on a chapter boundary, which is the case
  //    the tests found to be lossy. Included so the probe shows what the wire
  //    actually carries for it.
  {
    const anx::ProgressBridge::DevicePosition pos{0, 11, 12};  // last page of ch.0
    const double pct = anx::ProgressBridge::positionToPercentage(kCumulative, pos);
    device.push_back({"file/11112222333344445555666677778888.epub", pct, 1762923800});
  }

  // The phone has one of these books already, at an older position.
  anx::ProgressDocument phone;
  phone.schema = anx::ProgressJson::kSchemaVersion;
  phone.device = "phone";
  phone.updatedAt = 1762923000;
  phone.books.push_back({"file/aaaa1111bbbb2222cccc3333dddd4444.epub", 0.10, 1762923000});
  // ...and a book the device has never seen, which must not be uploaded.
  phone.books.push_back({"file/phone_only_book.epub", 0.90, 1762923900});

  const anx::ProgressDocument upload =
      anx::ProgressBridge::buildUploadDocument(device, phone, anx::ProgressJson::defaultDeviceName(), 1762924000);
  const std::string document = anx::ProgressJson::write(upload);

  // Round-trip through the parser as a self-check: if the writer and reader
  // disagree, the probe should fail rather than emit a document the phone will
  // reject.
  anx::ProgressDocument reparsed;
  const bool selfCheck = anx::ProgressJson::parse(document, reparsed);

  fprintf(stderr, "anx_contract_probe: %s entries, self-check %s\n",
          std::to_string(upload.books.size()).c_str(), selfCheck ? "PASS" : "FAIL");

  if (jsonMode) {
    // A tiny JSON wrapper so the Python side can locate the document without
    // having to guess where it starts.
    printf("{\"schema_version\": %d, \"device_name\": \"%s\", \"self_check\": %s, \"document\": ",
           anx::ProgressJson::kSchemaVersion, anx::ProgressJson::defaultDeviceName(),
           selfCheck ? "true" : "false");
  }

  printf("%s", document.c_str());

  if (jsonMode) printf("}\n");
  return selfCheck ? 0 : 1;
}
