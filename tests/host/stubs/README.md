# Host stubs

Stand-ins for the Arduino / ESP-IDF / crosspoint headers that `lib/AnxSyncIo`
depends on, so its translation units can be compiled and linked on a developer
machine. Used by `../check_io_layer.py`.

**These are not an emulator.** They provide just enough API surface to prove the
IO code parses, type-checks and links. Specifically:

* `esp_http_client_open()` always returns `ESP_FAIL`. A host run can therefore
  never look like a successful sync — that is deliberate.
* `HalStorage` reports no file exists and swallows writes.
* `base64::encode()` returns a fixed placeholder rather than real base64.

The richer, libcurl-backed `esp_http_client` shim that actually performs network
calls already exists at `firmware/simulator/shims/`. It is not used here because
it needs CMake and SDL2; this directory trades fidelity for "runs anywhere MSVC
exists".

Files:

| File | Real counterpart |
|---|---|
| `Arduino.h` | Arduino core + the crosspoint libs' public surface |
| `Arduino_stubs.cpp` | linkage for the above |
| `HalStorage.h` | `lib/hal/HalStorage.h` |
| `Logging.h` | `lib/Logging/Logging.h` |
| `Memory.h` | `lib/Memory/Memory.h` |
| `base64.h` | the base64 dependency |
| `esp_http_client.h`, `esp_crt_bundle.h` | ESP-IDF |
| `io_smoke_main.cpp` | the driver run after linking |
