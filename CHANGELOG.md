# Changelog

All notable changes to this project are documented in this file.

## [1.1.0] - 2026-10-01

### Fixed

- `Esp32RmtOneWireBus::Reset()` now automatically recovers from a wedged RMT RX channel. On some
  ESP32 variants (observed on ESP32-C5/C6), a failed reset pulse (`1-wire reset pulse receive
  timeout` / `rmt_receive: channel not in enable state`) previously left the hardware bus permanently
  unusable - every subsequent `Reset()` (and therefore every `MatchRom()`/`SkipRom()`/device read)
  would fail identically for the remainder of the sketch's lifetime, requiring a device reboot to
  recover. `Reset()` now detects the failure, recreates the underlying RMT bus handle, and retries
  once before reporting failure to the caller.

### Notes

- No public API changes. Existing callers (e.g. `Esp32RmtDs18b20Sensor`) are unaffected.
- A single ESP-IDF log line (`E (...) 1-wire.rmt: ...`) may still appear at the moment of the
  original failure - this is emitted directly by `onewire_bus` before the recovery logic runs and is
  expected/benign. See the README "Reliability" section for details.

## [1.0.0] - Initial release

- Full ROM search (Maxim/Dallas AN187 algorithm) with CRC validation.
- Bus diagnostics: idle-line level check, presence-pulse check, raw search-bit trace, direct Read ROM
  trace.
- Low-level primitives (`Reset`, `WriteByte`, `WriteBytes`, `ReadBytes`, `MatchRom`, `SkipRom`,
  `ComputeCrc8`).
