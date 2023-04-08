// 
// 
// 
#include <ConfigurableFirmata.h>
#include "EspSleep.h"


void EspSleep::reset()
{
	// Do not reset members, we want to go to sleep on next disconnect
	_lastDisconnect = 0;
}

boolean EspSleep::handleSysex(byte command, byte argc, byte* argv)
{
	// This command is very short, so that it would be an error for all other Scheduler modules
	if (command == SCHEDULER_DATA && argc == 2)
	{
		_goToSleepAfterDisconnect = argv[0] & 1;
		_sleepTimeout = 1000 * 60 * argv[1]; // Timeout in minutes, converted to ms
		if (_goToSleepAfterDisconnect)
		{
			Firmata.sendStringf(F("Sleep mode activated after %d ms"), _sleepTimeout);
		}
		return true;
	}

	return false;
}

void EspSleep::Update(bool isCurrentlyConnected)
{
	if (isCurrentlyConnected)
	{
		_lastDisconnect = 0;
		return;
	}

	if (_lastDisconnect == 0)
	{
		_lastDisconnect = millis();
	}

	if (!_goToSleepAfterDisconnect)
	{
		return;
	}

	uint64_t target = (uint64_t)_lastDisconnect + _sleepTimeout;
	uint64_t now = millis();

	if (now < _lastDisconnect || now > target)
	{
		EnterSleepMode();
	}
}

void EspSleep::EnterSleepMode()
{
	esp_sleep_enable_ext0_wakeup((gpio_num_t)_wakeupPin, _triggerValue); //1 = High, 0 = Low
	// If you were to use ext1, you would use it like
	// esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);

	//Go to sleep now
	Serial.println("CPU entering deep sleep mode.");
	esp_deep_sleep_start();
	Serial.println("This will never be printed");
}

void EspSleep::handleCapability(byte pin)
{
}

boolean EspSleep::handlePinMode(byte pin, int mode)
{
	return false;
}
