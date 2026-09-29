/*
 * Scan.ino
 *
 * Minimal example: scans the 1-Wire bus and prints the ROM address, family
 * code, and CRC status of every device found.
 */

#include <Esp32RmtOneWireBus.h>

// Change this to the GPIO pin connected to your 1-Wire data line.
#define ONE_WIRE_BUS 2

Esp32RmtOneWireBus bus(ONE_WIRE_BUS);

void setup()
{
	Serial.begin(115200);
	delay(1000);

	Serial.println("Looking for 1-Wire devices...");

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

void loop()
{
}
