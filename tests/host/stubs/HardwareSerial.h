#pragma once
// Host stub: Arduino's HardwareSerial. The real lib/Logging/Logging.h needs the
// type to declare its serial sink; nothing on the host writes to it.

#include <cstddef>
#include <cstdint>
#include <cstdio>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t b) { return write(&b, 1); }
  virtual size_t write(const uint8_t* buffer, size_t size) {
    static_cast<void>(buffer);
    return size;
  }
};

class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
};

class HardwareSerial : public Stream {
 public:
  void begin(unsigned long) {}
  void end() {}
  int availableForWrite() { return 0; }
  void flush() {}
  size_t print(const char*) { return 0; }
};

extern HardwareSerial Serial;
