/*
 * Diagnostics.ino
 *
 * Full bus-health trace: idle line level, presence attempts, direct Read
 * ROM, raw search bits, then a full ROM search. Useful for tracking down
 * wiring/timing issues on a 1-Wire bus.
 */

#include <Esp32RmtOneWireBus.h>

// Change this to the GPIO pin connected to your 1-Wire data line.
#define ONE_WIRE_BUS 2

Esp32RmtOneWireBus bus(ONE_WIRE_BUS);

void PrintScanReport()
{
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

void RunDiagnostics()
{
	Serial.println("=== 1-Wire diagnostics ===");

	Serial.print("Idle line level (should be HIGH via pull-up): ");
	Serial.println(bus.ReadIdleLevel() ? "HIGH (ok)" : "LOW (SHORT or missing/broken pull-up!)");

	Serial.println("Testing reset/presence pulse (5 attempts)...");
	int presenceCount = 0;
	for (int i = 0; i < 5; i++)
	{
		bool present = bus.CheckPresence();
		Serial.print("  attempt ");
		Serial.print(i);
		Serial.print(": ");
		Serial.println(present ? "presence detected" : "NO presence");
		if (present)
			presenceCount++;
		delay(200);
	}

	if (presenceCount == 0)
	{
		Serial.println();
		Serial.println("No presence pulse ever detected - this points to a wiring/hardware");
		Serial.println("issue (wrong pin, no pull-up, bad connection, dead sensor) rather than");
		Serial.println("a ROM-search bug. Skipping full scan.");
		return;
	}

	Serial.println();
	Serial.println("Presence OK - trying direct Read ROM (single-device test)...");
	bus.TraceReadRom();

	Serial.println();
	Serial.println("Tracing raw search bits...");
	bus.TraceSearch(16);

	Serial.println();
	Serial.println("Running full ROM search...");
	PrintScanReport();
}

void setup()
{
	Serial.begin(115200);
	delay(1000);

	RunDiagnostics();
}

void loop()
{
}
