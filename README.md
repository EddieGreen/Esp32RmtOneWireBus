# Esp32RmtOneWire

Hardware-timed 1-Wire bus driver for ESP32, backed by the RMT peripheral.

## Why

Bit-banging the 1-Wire protocol with Arduino GPIO calls (`pinMode`/`digitalWrite`/`digitalRead`/`delayMicroseconds`)
proved unreliable on newer ESP32-C5/C6 silicon: those calls carry enough GPIO-matrix/critical-section
overhead that microsecond-level bus timing was missed even from inside a FreeRTOS critical section,
giving inconsistent results between chips and even between runs on the same chip.

This library uses Espressif's `onewire_bus` component (vendored under `src/`) to generate the
reset/read/write time slots entirely in hardware via the RMT peripheral, removing any dependency on
CPU/GPIO-call timing.

## Features

- Full ROM search (Maxim/Dallas AN187 algorithm) with CRC validation.
- Bus diagnostics: idle-line level check, presence-pulse check, raw search-bit trace, direct Read ROM trace.
- Low-level primitives (`Reset`, `WriteByte`, `WriteBytes`, `ReadBytes`, `MatchRom`, `SkipRom`, `ComputeCrc8`)
  so device-specific libraries can issue their own commands without depending on `onewire_bus` internals
  directly. See [Esp32RmtDs18b20](https://github.com/EddieGreen/Esp32RmtDs18b20) for an example.

## Requirements

- ESP32 Arduino core (ESP32-only; uses the ESP-IDF RMT peripheral via `onewire_bus`).
- A 1-Wire bus with a pull-up resistor (4.7k-10k to 3.3V recommended). The internal weak pull-up is
  enabled as a fallback but is not a substitute for a proper external pull-up.

## Usage

```cpp
#include <Esp32RmtOneWireBus.h>

Esp32RmtOneWireBus bus(2); // GPIO pin connected to the 1-Wire data line

void setup()
{
	Serial.begin(115200);
	delay(1000);

	int count = bus.Scan();
	Serial.print(count);
	Serial.println(" device(s) found:");

	for (int i = 0; i < count; i++)
	{
		Serial.print("  [");
		Serial.print(i);
		Serial.print("] ");
		bus.PrintAddress(i);
		Serial.print("  family=0x");
		Serial.print(bus.GetFamilyCode(i), HEX);
		Serial.print("  crcOk=");
		Serial.println(bus.IsCrcValid(i) ? "yes" : "NO");
	}
}

void loop() {}
```

See `examples/Scan` for a minimal sketch and `examples/Diagnostics` for a full bus-health trace
(idle level, presence attempts, Read ROM, raw search bits) useful when tracking down wiring/timing issues.

## License

Original code is licensed under the MIT License (see `LICENSE`).

This library vendors portions of Espressif's `onewire_bus` component under the Apache License,
Version 2.0. See `NOTICE` for the list of affected files; those files retain their original
SPDX headers and are not covered by the MIT license.
