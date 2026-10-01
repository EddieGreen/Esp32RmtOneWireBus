#pragma once

/*
 * Esp32RmtOneWireBus.h - Hardware-timed 1-Wire bus driver for ESP32 (RMT-backed).
 *
 * Standalone utility, independent of the OneWire/DallasTemperature libraries
 * (which fail to compile on some newer ESP32 variants, e.g. ESP32-C5, due to
 * unsupported direct-register GPIO access). Timing-critical reset/read/write
 * slots are generated in hardware via Espressif's onewire_bus RMT driver
 * (vendored under src/onewire_bus_*), rather than bit-banged with Arduino GPIO
 * calls - this avoids CPU-timing fragility (GPIO-matrix/critical-section
 * overhead) that made plain pinMode/digitalWrite/delayMicroseconds unreliable
 * on newer ESP32-C5/C6 silicon. ESP32-only.
 *
 * Provides ROM discovery (Maxim/Dallas AN187 search), bus diagnostics, and a
 * set of low-level primitives (Reset/WriteByte/WriteBytes/ReadBytes/
 * MatchRom/SkipRom/ComputeCrc8) so device-specific libraries (e.g.
 * Esp32RmtDs18b20) can issue their own commands on top of this bus without
 * depending on ESP-IDF/onewire_bus internals directly.
 *
 * Usage (in a throwaway diagnostic sketch):
 *
 *   #include <Esp32RmtOneWireBus.h>
 *
 *   Esp32RmtOneWireBus bus(2); // GPIO pin connected to the 1-Wire data line
 *
 *   void setup()
 *   {
 *       Serial.begin(115200);
 *       delay(1000);
 *
 *       int count = bus.Scan();
 *       Serial.print(count);
 *       Serial.println(" device(s) found:");
 *
 *       for (int i = 0; i < count; i++)
 *       {
 *           Serial.print("  [");
 *           Serial.print(i);
 *           Serial.print("] ");
 *           bus.PrintAddress(i);
 *           Serial.print("  family=0x");
 *           Serial.print(bus.GetFamilyCode(i), HEX);
 *           Serial.print("  crcOk=");
 *           Serial.println(bus.IsCrcValid(i) ? "yes" : "NO");
 *       }
 *   }
 *
 *   void loop() {}
 */

#include <Arduino.h>

#ifndef ONE_WIRE_SCANNER_MAX_DEVICES
#define ONE_WIRE_SCANNER_MAX_DEVICES 16
#endif

class Esp32RmtOneWireBus
{
public:
	explicit Esp32RmtOneWireBus(uint8_t pin) : busPin(pin) {}
	~Esp32RmtOneWireBus();

	// Performs a full 1-Wire ROM search and stores discovered addresses.
	// Returns the number of devices found (capped at ONE_WIRE_SCANNER_MAX_DEVICES).
	int Scan();

	// --- Diagnostics -------------------------------------------------------

	// Reads the raw idle-state level of the bus (pin held as INPUT, no drive).
	// Should read HIGH if the pull-up resistor and wiring are healthy.
	// Reads LOW if the bus is shorted to ground, or the pull-up is missing/broken.
	bool ReadIdleLevel() const;

	// Issues a single reset pulse and reports whether any device answered with
	// a presence pulse. Does not perform a ROM search - useful to isolate
	// wiring/timing problems from ROM-search logic problems.
	bool CheckPresence() const;

	// Resets, sends the Search ROM command, then reads and prints the raw
	// (idBit, complementBit) pair for each of the first `bitCount` bits,
	// writing back 0 as the arbitrary search direction each time. Useful to
	// see exactly where/why a search fails (e.g. constant 1/1 = no response).
	void TraceSearch(int bitCount = 16) const;

	// Simplest possible round-trip test: reset, send Read ROM (0x33), then
	// read back 8 bytes with plain sequential reads (no discrepancy tracking).
	// Only valid with exactly one device on the bus, but isolates whether
	// basic write-byte/read-byte bit-banging works at all. Prints the result
	// and returns whether the CRC of the returned ROM is valid.
	bool TraceReadRom() const;

	// Number of devices found by the last Scan() call.
	int GetDeviceCount() const { return deviceCount; }

	// Raw 8-byte ROM address for a discovered device (Family + 6 ROM bytes + CRC).
	const uint8_t* GetAddress(int index) const { return addresses[index]; }

	// First byte of the ROM address - identifies the device type
	// (e.g. 0x28 = DS18B20, 0x10 = DS18S20, 0x22 = DS1822).
	uint8_t GetFamilyCode(int index) const { return addresses[index][0]; }

	// True if the trailing CRC byte matches the computed CRC of the first 7 bytes.
	bool IsCrcValid(int index) const;

	// Prints one address in colon-separated hex, e.g. 28:3F:33:6E:04:16:03:AC
	void PrintAddress(int index) const;

	// Prints every discovered address (one per line), preceded by its index.
	void PrintAll() const;

	// --- Low-level primitives (for device-specific libraries) --------------

	// Computes the standard Maxim/Dallas 8-bit CRC over `length` bytes.
	static uint8_t ComputeCrc8(const uint8_t* data, uint8_t length);

	// Issues a reset pulse and reports whether a device answered with presence.
	// Lazily creates the hardware bus handle on first use. If the underlying
	// RMT channel pair has wedged (see Reset() implementation notes), this
	// transparently recreates the bus handle and retries once.
	bool Reset() const;

	// Writes a single byte, least-significant bit first. Must follow a
	// successful Reset(). Returns false on a bus/write error.
	bool WriteByte(uint8_t data) const;

	// Writes `length` bytes, least-significant bit first. Returns false on a
	// bus/write error.
	bool WriteBytes(const uint8_t* data, size_t length) const;

	// Reads `length` bytes, least-significant bit first, into `outData`.
	// Returns false on a bus/read error.
	bool ReadBytes(uint8_t* outData, size_t length) const;

	// Resets the bus and addresses a single device via Match ROM (0x55),
	// leaving the bus ready for a following command byte. Returns false if
	// no presence pulse was seen or any write failed.
	bool MatchRom(const uint8_t* romAddress) const;

	// Resets the bus and broadcasts Skip ROM (0xCC), addressing every device
	// simultaneously. Returns false if no presence pulse was seen or the
	// write failed.
	bool SkipRom() const;

private:
	// Lazily creates the hardware (RMT-backed) 1-Wire bus handle on first use.
	// Returns true if the handle is ready to use.
	bool EnsureBus() const;

	// Tears down and recreates the hardware bus handle (used to recover from
	// a wedged RMT channel - see Reset() implementation notes).
	void RecreateBus() const;

	uint8_t busPin;
	mutable void* busHandle = nullptr; // onewire_bus_handle_t, opaque here to keep this header portable
	uint8_t addresses[ONE_WIRE_SCANNER_MAX_DEVICES][8] = {};
	int deviceCount = 0;
};
