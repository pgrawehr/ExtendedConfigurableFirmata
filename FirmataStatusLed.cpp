#include <ConfigurableFirmata.h>
#include <FirmataExt.h>
#include "FirmataStatusLed.h"

void FirmataStatusLed::Init()
{
	// Remove the pin from the default firmata functions
	Firmata.setPinMode(_pin, PIN_MODE_IGNORE);
	pinMode(_pin, OUTPUT);
	digitalWrite(_pin, 0);
	_triggerCount = 0;
	_isOn = false;
	_lastTime = micros();
	_minTime = 0x7fffffff;
}

boolean FirmataStatusLed::handleSysex(byte command, byte argc, byte* argv)
{
	return false;
}

void FirmataStatusLed::report(bool elapsed)
{
	uint32_t newTime = micros();
	// Rollover - just ignore this round
	if (newTime < _lastTime)
	{
		_lastTime = newTime;
		return;
	}

	uint32_t diff = newTime - _lastTime;
	if (diff <= _minTime)
	{
		_minTime = diff;
		if (_minTime <= 1)
		{
			// Otherwise, we can't divide below
			// We also get here if diff calculates as zero above
			_minTime = 1;
			diff = 1;
		}
	}

	_lastTime = newTime;

	if (!elapsed)
	{
		return;
	}

	uint32_t divisor = (100 * _minTime) / diff;

	if (divisor < 2)
	{
		divisor = 2;
	}

	_triggerCount++;

	if ((_triggerCount % divisor) == 0)
	{
		_isOn = !_isOn;
		digitalWrite(_pin, _isOn);
	}
}
