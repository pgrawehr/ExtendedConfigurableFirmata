#include <ConfigurableFirmata.h>
#include <FirmataExt.h>
#include "FirmataStatusLed.h"

FirmataStatusLed* FirmataStatusLed::FirmataStatusLedInstance = nullptr;

void FirmataStatusLed::Init()
{
	// Remove the pin from the default firmata functions
	// This will also prevent the pin from being reported as available for GPIO
	Firmata.setPinMode(_pin, PIN_MODE_IGNORE);
	pinMode(_pin, OUTPUT);
	digitalWrite(_pin, 1);
	_startClock = millis();
	_resetAt = 0;
	_status = STATUS_IDLE;
	_isOn = true;
}

boolean FirmataStatusLed::handleSysex(byte command, byte argc, byte* argv)
{
	return false;
}

void FirmataStatusLed::report(bool elapsed)
{
	// By default, this is true every 19ms, which is accurate enough for this component
	if (!elapsed)
	{
		return;
	}

	uint32_t newTime = millis();
	if (newTime < _startClock)
	{
		// Wrapped around (this will give a slight glitch, but this is irrelevant)
		_startClock = newTime;
		return;
	}

	uint32_t delta = newTime - _startClock;
	int sumOfSteps = 0; // The total length of the sequence for the current status

	for (int i = 0; i < STATUS_NUMBER_OF_STEPS; i++)
	{
		sumOfSteps += BlinkPatterns[_status][i];
	}

	delta = delta % sumOfSteps;
	uint32_t ticksWithinSequence = 0;

	int index = -1;
	while (ticksWithinSequence <= delta && index < STATUS_NUMBER_OF_STEPS)
	{
		index++;
		ticksWithinSequence += BlinkPatterns[_status][index];
	}

	bool shouldBeOn = (index % 2) == 0;

	if (shouldBeOn != _isOn)
	{
		digitalWrite(_pin, shouldBeOn);
		_isOn = shouldBeOn;
	}

	if (_resetAt <= newTime && _resetAt != 0)
	{
		setStatus(STATUS_IDLE, 0);
	}
}

void FirmataStatusLed::setStatus(int status, int resetAfter)
{
	if (resetAfter != 0)
	{
		_resetAt = millis() + resetAfter;
	}
	else
	{
		_resetAt = 0;
	}

	if (status != _status)
	{
		_status = status;
		_startClock = millis();
	}
}

