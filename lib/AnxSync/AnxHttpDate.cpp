#include "AnxHttpDate.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace anx {

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date.
// Uses the civil-from-days algorithm (Howard Hinnant), valid for any year.
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);              // [0, 399]
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                // [0, 146096]
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

/**
 * Read up to `maxDigits` decimal digits from `p`, advancing `p`.
 *
 * Stops cleanly at `maxDigits` rather than failing: callers rely on this to
 * consume a fixed-width field (e.g. the "00" of a "+0100" offset) and then
 * inspect the following character themselves. Fails only when no digit is
 * present at all.
 */
bool parseUInt(const char*& p, int maxDigits, int& outValue) {
  int value = 0;
  int digits = 0;
  while (digits < maxDigits && *p != '\0' && std::isdigit(static_cast<unsigned char>(*p)) != 0) {
    value = value * 10 + (*p - '0');
    ++p;
    ++digits;
  }
  if (digits == 0) return false;
  outValue = value;
  return true;
}

void skipSpaces(const char*& p) {
  while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)) != 0) ++p;
}

/** Case-insensitive compare of the 3-letter month abbreviation. */
int monthFromName(const char* name) {
  static const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i) {
    if (std::strncmp(name, kMonths[i], 3) == 0) return i + 1;
  }
  return 0;
}

/**
 * Parse the timezone suffix.
 *
 * Accepted forms:
 *   "GMT" / "UTC"
 *   "+0100" / "-0500"   (RFC 1123 / RFC 822 style: +HHMM, the common case)
 *   "+01:00"            (ISO-ish variant emitted by a few servers)
 *
 * Only valid offsets are accepted; anything else is a parse failure so a
 * timezone bug cannot silently shift every book's mtime by hours.
 */
bool parseZone(const char* p, int& outOffsetSeconds) {
  skipSpaces(p);
  if (*p == '\0') return false;

  if (std::strncmp(p, "GMT", 3) == 0 || std::strncmp(p, "UTC", 3) == 0) {
    outOffsetSeconds = 0;
    return true;
  }
  if (*p == '+' || *p == '-') {
    const int sign = (*p == '-') ? -1 : 1;
    ++p;

    int hh = 0, mm = 0;
    if (!parseUInt(p, 2, hh)) return false;

    if (*p == ':') {
      ++p;
      if (!parseUInt(p, 2, mm)) return false;
    } else if (std::isdigit(static_cast<unsigned char>(*p)) != 0) {
      // "+HHMM": the minute digits immediately follow the hour digits.
      if (!parseUInt(p, 2, mm)) return false;
    } else if (*p != '\0') {
      return false;
    }

    if (hh > 23 || mm > 59) return false;
    outOffsetSeconds = sign * (hh * 3600 + mm * 60);
    return true;
  }
  return false;
}

}  // namespace

bool HttpDate::isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int HttpDate::daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return kDays[month - 1];
}

bool HttpDate::toUnixTime(int year, int month, int day, int hour, int minute, int second, int64_t& outUtc) {
  if (year < 1900 || year > 9999) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > daysInMonth(year, month)) return false;
  if (hour < 0 || hour > 23) return false;
  if (minute < 0 || minute > 59) return false;
  if (second < 0 || second > 60) return false;  // 60 tolerates leap seconds

  const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  outUtc = days * 86400 + static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 + second;
  return true;
}

bool HttpDate::parseRfc1123(const char* text, int64_t& outUtc) {
  if (text == nullptr) return false;

  const char* p = text;
  skipSpaces(p);
  if (*p == '\0') return false;

  // 1. Skip an optional weekday token ("Sun," / "Sunday, 06-Nov-94 ...").
  //    Only skip when the token is followed by a comma; otherwise "06 Nov ..."
  //    would lose its leading day number.
  bool skippedWeekday = false;
  if (std::isalpha(static_cast<unsigned char>(*p)) != 0) {
    const char* probe = p;
    while (*probe != '\0' && std::isalpha(static_cast<unsigned char>(*probe)) != 0) ++probe;
    if (*probe == ',') {
      p = probe + 1;
      skipSpaces(p);
      skippedWeekday = true;
    }
  }
  (void)skippedWeekday;

  // 2. Day of month.
  int day = 0;
  if (!parseUInt(p, 2, day)) return false;

  // Separator after the day is either whitespace ("06 Nov") or a dash
  // ("06-Nov-94"). Anything else means this isn't a date we understand.
  if (*p == '-') {
    ++p;
  } else if (std::isspace(static_cast<unsigned char>(*p)) != 0) {
    skipSpaces(p);
  } else {
    return false;
  }

  // 3. Month name.
  if (std::isalpha(static_cast<unsigned char>(*p)) == 0) return false;
  const int month = monthFromName(p);
  if (month == 0) return false;
  while (std::isalpha(static_cast<unsigned char>(*p)) != 0) ++p;

  // 4. Separator then year (2 or 4 digits).
  if (*p == '-') {
    ++p;
  } else if (std::isspace(static_cast<unsigned char>(*p)) != 0) {
    skipSpaces(p);
  } else {
    return false;
  }

  int year = 0;
  if (!parseUInt(p, 4, year)) return false;
  if (year < 100) year += (year < 70) ? 2000 : 1900;  // RFC 850 two-digit year

  // 5. Time "HH:MM:SS".
  skipSpaces(p);
  int hour = 0, minute = 0, second = 0;
  if (!parseUInt(p, 2, hour)) return false;
  if (*p != ':') return false;
  ++p;
  if (!parseUInt(p, 2, minute)) return false;
  if (*p != ':') return false;
  ++p;
  if (!parseUInt(p, 2, second)) return false;

  // 6. Timezone.
  int zoneOffset = 0;
  if (!parseZone(p, zoneOffset)) return false;

  int64_t utc = 0;
  if (!toUnixTime(year, month, day, hour, minute, second, utc)) return false;
  outUtc = utc - zoneOffset;  // "+0100" means local time is one hour ahead of UTC
  return true;
}

bool HttpDate::formatRfc1123(int64_t utc, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) return false;

  static const char* kWeekdays[7] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
  static const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  int64_t days = utc / 86400;
  int64_t secsOfDay = utc % 86400;
  if (secsOfDay < 0) {  // floor division for pre-1970 timestamps
    secsOfDay += 86400;
    days -= 1;
  }

  // civil_from_days (Howard Hinnant)
  int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);

  // 1970-01-01 was a Thursday, so day 0 -> index 0 in kWeekdays.
  // Modulo must be non-negative; days is already floored above.
  const int weekday = static_cast<int>(((days % 7) + 7) % 7);
  const int hour = static_cast<int>(secsOfDay / 3600);
  const int minute = static_cast<int>((secsOfDay % 3600) / 60);
  const int second = static_cast<int>(secsOfDay % 60);

  const int written = std::snprintf(out, outSize, "%s, %02u %s %04lld %02d:%02d:%02d GMT", kWeekdays[weekday],
                                    d, kMonths[m - 1], static_cast<long long>(y), hour, minute, second);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace anx
