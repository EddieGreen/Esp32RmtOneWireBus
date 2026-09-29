#include "Esp32RmtOneWireBus.h"

// ---------------------------------------------------------------------------
// 1-Wire bus primitives + ROM search algorithm, backed by hardware timing.
//
// Bit-banging the 1-Wire protocol with Arduino GPIO calls (pinMode/
// digitalWrite/digitalRead/delayMicroseconds) proved unreliable on newer
// ESP32-C5/C6 silicon: those calls carry enough GPIO-matrix/critical-section
// overhead that microsecond-level bus timing was blown even from inside a
// FreeRTOS critical section, giving inconsistent results between chips and
// even between runs on the same chip.
//
// Espressif's onewire_bus component (vendored under src/onewire_bus_*)
// instead generates the reset/read/write time slots entirely in hardware
// using the RMT peripheral, removing any dependency on CPU/GPIO-call timing.
// This file implements the ROM search algorithm (Maxim/Dallas AN187) plus a
// set of public primitives on top of the hardware-backed reset/read-bit/
// write-bit calls it exposes, so other libraries can issue their own device
// commands (e.g. DS18B20 Convert T/Read Scratchpad) without depending on
// onewire_bus directly.
// ---------------------------------------------------------------------------
#if !defined(ARDUINO_ARCH_ESP32)
#error "Esp32RmtOneWireBus requires the ESP32 Arduino core (uses the onewire_bus RMT driver)."
#endif

#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_crc.h"

namespace
{
	constexpr uint8_t CMD_SEARCH_ROM = 0xF0;
	constexpr uint8_t CMD_READ_ROM = 0x33;
	constexpr uint8_t CMD_MATCH_ROM = 0x55;
	constexpr uint8_t CMD_SKIP_ROM = 0xCC;

	inline onewire_bus_handle_t ToHandle(void* handle)
	{
		return static_cast<onewire_bus_handle_t>(handle);
	}

	bool ReadBit(onewire_bus_handle_t bus, uint8_t& outBit)
	{
		return onewire_bus_read_bit(bus, &outBit) == ESP_OK;
	}

	bool WriteBit(onewire_bus_handle_t bus, uint8_t bit)
	{
		return onewire_bus_write_bit(bus, bit) == ESP_OK;
	}

	bool WriteByteInternal(onewire_bus_handle_t bus, uint8_t data)
	{
		return onewire_bus_write_bytes(bus, &data, 1) == ESP_OK;
	}

	// Single search step of the AN187 algorithm.
	// lastDiscrepancy/romNo carry search state between successive calls so the
	// full device tree can be enumerated one leaf at a time.
	// Returns true if a device address was found this call, false when the
	// search is exhausted (or on a bus/CRC error).
	bool SearchStep(onewire_bus_handle_t bus, uint8_t* romNo, int& lastDiscrepancy, bool& lastDeviceFlag)
	{
		if (lastDeviceFlag)
			return false;

		esp_err_t resetResult = onewire_bus_reset(bus);
		if (resetResult != ESP_OK)
		{
			lastDiscrepancy = 0;
			lastDeviceFlag = false;
			return false;
		}

		if (!WriteByteInternal(bus, CMD_SEARCH_ROM))
			return false;

		int idBitNumber = 1;
		int lastZero = 0;
		uint8_t byteIndex = 0;
		uint8_t bitMask = 1;
		bool searchResult = false;

		while (idBitNumber <= 64)
		{
			uint8_t idBit = 0;
			uint8_t cmpIdBit = 0;
			if (!ReadBit(bus, idBit) || !ReadBit(bus, cmpIdBit))
				break;

			if (idBit == 1 && cmpIdBit == 1)
			{
				// No devices responded - bus error.
				break;
			}

			uint8_t searchDirection;
			if (idBit != cmpIdBit)
			{
				// All devices agree on this bit.
				searchDirection = idBit;
			}
			else
			{
				// Discrepancy - devices disagree on this bit.
				if (idBitNumber < lastDiscrepancy)
					searchDirection = ((romNo[byteIndex] & bitMask) > 0) ? 1 : 0;
				else
					searchDirection = (idBitNumber == lastDiscrepancy) ? 1 : 0;

				if (searchDirection == 0)
					lastZero = idBitNumber;
			}

			if (searchDirection == 1)
				romNo[byteIndex] |= bitMask;
			else
				romNo[byteIndex] &= ~bitMask;

			if (!WriteBit(bus, searchDirection))
				break;

			idBitNumber++;
			bitMask <<= 1;
			if (bitMask == 0)
			{
				bitMask = 1;
				byteIndex++;
			}
		}

		if (idBitNumber > 64)
		{
			lastDiscrepancy = lastZero;
			if (lastDiscrepancy == 0)
				lastDeviceFlag = true;
			searchResult = true;
		}

		if (!searchResult || romNo[0] == 0)
		{
			lastDiscrepancy = 0;
			lastDeviceFlag = false;
			searchResult = false;
		}

		return searchResult;
	}
}

Esp32RmtOneWireBus::~Esp32RmtOneWireBus()
{
	if (busHandle != nullptr)
	{
		onewire_bus_del(ToHandle(busHandle));
		busHandle = nullptr;
	}
}

bool Esp32RmtOneWireBus::EnsureBus() const
{
	if (busHandle != nullptr)
		return true;

	onewire_bus_config_t busConfig = {};
	busConfig.bus_gpio_num = busPin;
	busConfig.flags.en_pull_up = 1; // internal pull-up as a fallback; external pull-up still recommended

	onewire_bus_rmt_config_t rmtConfig = {};
	rmtConfig.max_rx_bytes = 10; // enough for reset + a full ROM (8 bytes) reply

	onewire_bus_handle_t bus = nullptr;
	esp_err_t result = onewire_new_bus_rmt(&busConfig, &rmtConfig, &bus);
	if (result != ESP_OK)
	{
		Serial.print("Esp32RmtOneWireBus: failed to create RMT 1-Wire bus, esp_err=");
		Serial.println(result);
		return false;
	}

	busHandle = bus;
	return true;
}

uint8_t Esp32RmtOneWireBus::ComputeCrc8(const uint8_t* data, uint8_t length)
{
	return onewire_crc8(0, const_cast<uint8_t*>(data), length);
}

bool Esp32RmtOneWireBus::Reset() const
{
	if (!EnsureBus())
		return false;

	return onewire_bus_reset(ToHandle(busHandle)) == ESP_OK;
}

bool Esp32RmtOneWireBus::WriteByte(uint8_t data) const
{
	if (!EnsureBus())
		return false;

	return WriteByteInternal(ToHandle(busHandle), data);
}

bool Esp32RmtOneWireBus::WriteBytes(const uint8_t* data, size_t length) const
{
	if (!EnsureBus())
		return false;

	return onewire_bus_write_bytes(ToHandle(busHandle), data, length) == ESP_OK;
}

bool Esp32RmtOneWireBus::ReadBytes(uint8_t* outData, size_t length) const
{
	if (!EnsureBus())
		return false;

	return onewire_bus_read_bytes(ToHandle(busHandle), outData, length) == ESP_OK;
}

bool Esp32RmtOneWireBus::MatchRom(const uint8_t* romAddress) const
{
	if (!Reset())
		return false;

	if (!WriteByte(CMD_MATCH_ROM))
		return false;

	return WriteBytes(romAddress, 8);
}

bool Esp32RmtOneWireBus::SkipRom() const
{
	if (!Reset())
		return false;

	return WriteByte(CMD_SKIP_ROM);
}

int Esp32RmtOneWireBus::Scan()
{
	deviceCount = 0;

	if (!EnsureBus())
		return 0;

	onewire_bus_handle_t bus = ToHandle(busHandle);
	int lastDiscrepancy = 0;
	bool lastDeviceFlag = false;
	uint8_t romNo[8] = { 0 };

	while (deviceCount < ONE_WIRE_SCANNER_MAX_DEVICES)
	{
		if (!SearchStep(bus, romNo, lastDiscrepancy, lastDeviceFlag))
			break;

		memcpy(addresses[deviceCount], romNo, 8);
		deviceCount++;

		if (lastDeviceFlag)
			break;
	}

	return deviceCount;
}

bool Esp32RmtOneWireBus::ReadIdleLevel() const
{
	// The RMT-backed bus takes exclusive ownership of the pin once created.
	// Only meaningful before the hardware bus has been initialized.
	if (busHandle != nullptr)
	{
		Serial.println("ReadIdleLevel: bus already initialized by RMT driver - level check skipped.");
		return true;
	}

	// First check with the MCU's own internal pull-up disabled (true floating
	// read) - this reveals whether an *external* pull-up/pull-down is present
	// at all, independent of the ESP32's own (weak, ~45k) internal pull-up.
	pinMode(busPin, INPUT);
	delayMicroseconds(10);
	bool floatingHigh = digitalRead(busPin) == HIGH;

	// Then check with the internal pull-up enabled, matching what EnsureBus()
	// will later request from the RMT driver (flags.en_pull_up = 1).
	pinMode(busPin, INPUT_PULLUP);
	delayMicroseconds(10);
	bool pulledHigh = digitalRead(busPin) == HIGH;
	pinMode(busPin, INPUT);

	if (!floatingHigh && !pulledHigh)
	{
		Serial.println("ReadIdleLevel: LOW even with internal pull-up enabled - line is shorted to ground or a device is stuck holding it low.");
	}
	else if (!floatingHigh && pulledHigh)
	{
		Serial.println("ReadIdleLevel: floating LOW, but internal weak pull-up (~45k) can raise it - no (or too-weak) external pull-up present. Add a 4.7k (or similar) resistor to 3.3V for reliable bit-level timing.");
	}

	return floatingHigh;
}

bool Esp32RmtOneWireBus::CheckPresence() const
{
	return Reset();
}

void Esp32RmtOneWireBus::TraceSearch(int bitCount) const
{
	if (!EnsureBus())
	{
		Serial.println("TraceSearch: failed to initialize 1-Wire bus - aborting.");
		return;
	}

	onewire_bus_handle_t bus = ToHandle(busHandle);

	esp_err_t resetResult = onewire_bus_reset(bus);
	if (resetResult != ESP_OK)
	{
		Serial.print("TraceSearch: no presence pulse - aborting. esp_err=");
		Serial.println(resetResult);
		return;
	}

	WriteByteInternal(bus, CMD_SEARCH_ROM);

	for (int i = 0; i < bitCount; i++)
	{
		uint8_t idBit = 0;
		uint8_t cmpIdBit = 0;
		if (!ReadBit(bus, idBit) || !ReadBit(bus, cmpIdBit))
		{
			Serial.print("  bit ");
			Serial.print(i);
			Serial.println("  -> bus read error - stopping trace.");
			return;
		}

		Serial.print("  bit ");
		Serial.print(i);
		Serial.print(": id=");
		Serial.print(idBit);
		Serial.print(" cmp=");
		Serial.print(cmpIdBit);

		if (idBit == 1 && cmpIdBit == 1)
		{
			Serial.println("  -> NO RESPONSE (bus error) - stopping trace.");
			return;
		}
		else if (idBit != cmpIdBit)
		{
			Serial.print("  -> all devices agree, bit=");
			Serial.println(idBit);
		}
		else
		{
			Serial.println("  -> discrepancy (multiple devices), writing 0");
		}

		// Arbitrary direction: always steer toward 0 branch on discrepancy.
		WriteBit(bus, 0);
	}
}

bool Esp32RmtOneWireBus::TraceReadRom() const
{
	if (!EnsureBus())
	{
		Serial.println("TraceReadRom: failed to initialize 1-Wire bus - aborting.");
		return false;
	}

	onewire_bus_handle_t bus = ToHandle(busHandle);

	esp_err_t resetResult = onewire_bus_reset(bus);
	if (resetResult != ESP_OK)
	{
		Serial.print("TraceReadRom: no presence pulse - aborting. esp_err=");
		Serial.println(resetResult);
		return false;
	}

	WriteByteInternal(bus, CMD_READ_ROM);

	uint8_t rom[8] = { 0 };
	if (onewire_bus_read_bytes(bus, rom, sizeof(rom)) != ESP_OK)
	{
		Serial.println("TraceReadRom: read_bytes failed.");
		return false;
	}

	Serial.print("TraceReadRom: ");
	for (int i = 0; i < 8; i++)
	{
		if (rom[i] < 16)
			Serial.print("0");
		Serial.print(rom[i], HEX);
		if (i < 7)
			Serial.print(":");
	}

	bool crcOk = ComputeCrc8(rom, 7) == rom[7];
	Serial.print("  crcOk=");
	Serial.println(crcOk ? "yes" : "NO");

	return crcOk;
}

bool Esp32RmtOneWireBus::IsCrcValid(int index) const
{
	return ComputeCrc8(addresses[index], 7) == addresses[index][7];
}

void Esp32RmtOneWireBus::PrintAddress(int index) const
{
	const uint8_t* address = addresses[index];
	for (uint8_t byteIndex = 0; byteIndex < 8; byteIndex++)
	{
		if (address[byteIndex] < 16)
			Serial.print("0");
		Serial.print(address[byteIndex], HEX);
		if (byteIndex < 7)
			Serial.print(":");
	}
}

void Esp32RmtOneWireBus::PrintAll() const
{
	for (int i = 0; i < deviceCount; i++)
	{
		Serial.print("[");
		Serial.print(i);
		Serial.print("] ");
		PrintAddress(i);
		Serial.println();
	}
}
