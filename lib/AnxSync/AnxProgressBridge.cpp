#include "AnxProgressBridge.h"

#include <cmath>

#include "AnxProgressLocator.h"

namespace anx {

namespace {

const char* kLocalBooksPrefix = "/Books/";
const char* kAnxFilePrefix = "file/";

}  // namespace

double ProgressBridge::positionToPercentage(const std::vector<uint32_t>& cumulative,
                                            const DevicePosition& position) {
  if (cumulative.empty() || cumulative.back() == 0) return 0.0;

  // Same convention as ProgressLocator::pageToIntraSpine: the last page of an
  // item is 1.0, so the divisor is totalPages - 1 and a one-page item is 0.0.
  const float intra = ProgressLocator::pageToIntraSpine(position.pageNumber, position.totalPages);
  return static_cast<double>(ProgressLocator::toPercentage(cumulative, position.spineIndex, intra));
}

ProgressBridge::DevicePosition ProgressBridge::percentageToPosition(const std::vector<uint32_t>& cumulative,
                                                                   double percentage,
                                                                   const std::vector<int>& pagesPerSpine,
                                                                   bool* wasFallback) {
  if (wasFallback != nullptr) *wasFallback = false;

  DevicePosition result;

  // Reject non-finite input rather than letting it propagate through the float
  // cast into an arbitrary spine index.
  if (!std::isfinite(percentage)) {
    if (wasFallback != nullptr) *wasFallback = true;
    return result;
  }

  const ProgressLocator::Location loc = ProgressLocator::locate(cumulative, static_cast<float>(percentage));
  if (loc.isFallback) {
    if (wasFallback != nullptr) *wasFallback = true;
    return result;
  }

  result.spineIndex = loc.spineIndex;

  // An item that has not been laid out yet has no page count. Treat it as a
  // single page so the caller still gets a valid spine index; the reader
  // recomputes the page as soon as the section is rendered.
  int totalPages = 1;
  if (loc.spineIndex >= 0 && static_cast<size_t>(loc.spineIndex) < pagesPerSpine.size()) {
    const int candidate = pagesPerSpine[static_cast<size_t>(loc.spineIndex)];
    if (candidate > 0) totalPages = candidate;
  }

  result.totalPages = totalPages;
  result.pageNumber = ProgressLocator::intraSpineToPage(loc.intraSpine, totalPages);
  return result;
}

bool ProgressBridge::localPathToAnxFilePath(const std::string& localPath, std::string& out) {
  const std::string prefix = kLocalBooksPrefix;
  if (localPath.size() <= prefix.size()) return false;
  if (localPath.compare(0, prefix.size(), prefix) != 0) return false;

  const std::string name = localPath.substr(prefix.size());
  // Anything with a separator left in it is not a plain book name.
  if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
    return false;
  }

  out = std::string(kAnxFilePrefix) + name;
  return true;
}

ProgressBridge::Winner ProgressBridge::resolveConflict(bool hasDeviceEntry, double devicePercentage,
                                                       int64_t deviceUpdatedAt, bool hasPhoneEntry,
                                                       double phonePercentage, int64_t phoneUpdatedAt,
                                                       double epsilon) {
  if (!hasPhoneEntry) return Winner::Device;
  if (!hasDeviceEntry) return Winner::Phone;

  const double delta = std::fabs(devicePercentage - phonePercentage);
  if (delta < epsilon) return Winner::Same;

  if (deviceUpdatedAt > phoneUpdatedAt) return Winner::Device;
  if (deviceUpdatedAt < phoneUpdatedAt) return Winner::Phone;

  // Equal timestamps: the phone is the primary reading device, so it wins. That
  // way a device waking up with a stale clock cannot drag the phone backwards.
  return Winner::Phone;
}

int ProgressBridge::mergeFromPhone(const ProgressDocument& document,
                                   const std::vector<BookProgress>& deviceEntries,
                                   std::vector<std::string>& outWinners) {
  outWinners.clear();
  int updates = 0;

  for (const BookProgress& phone : document.books) {
    const BookProgress* device = nullptr;
    for (const BookProgress& entry : deviceEntries) {
      if (entry.filePath == phone.filePath) {
        device = &entry;
        break;
      }
    }

    const Winner winner =
        resolveConflict(device != nullptr, device != nullptr ? device->percentage : 0.0,
                        device != nullptr ? device->updatedAt : 0, /*hasPhoneEntry=*/true, phone.percentage,
                        phone.updatedAt);

    if (winner == Winner::Phone) {
      outWinners.push_back(phone.filePath);
      ++updates;
    }
  }

  return updates;
}

ProgressDocument ProgressBridge::buildUploadDocument(const std::vector<BookProgress>& deviceEntries,
                                                     const ProgressDocument& phoneDocument,
                                                     const std::string& deviceName, int64_t now) {
  ProgressDocument doc;
  doc.schema = ProgressJson::kSchemaVersion;
  doc.device = deviceName.empty() ? std::string(ProgressJson::defaultDeviceName()) : deviceName;
  doc.updatedAt = now;

  for (const BookProgress& device : deviceEntries) {
    // Only report positions this device is authoritative about: if the phone has
    // no entry, or its entry is older, the device's value is worth uploading.
    const BookProgress* phone = ProgressJson::find(phoneDocument, device.filePath);
    if (phone != nullptr && phone->updatedAt >= device.updatedAt) continue;

    BookProgress upload = device;
    upload.percentage = ProgressJson::clampPercentage(device.percentage);
    doc.books.push_back(upload);
  }

  return doc;
}

}  // namespace anx
