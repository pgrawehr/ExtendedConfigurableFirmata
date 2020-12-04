// FirmataStatusLed.h

#ifndef _FirmataStatusLed_h
#define _FirmataStatusLed_h
#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

/// <summary>
/// Simple component that reports the CPU status by some blinking patterns.
/// By default, the VERSION_BLINK_PIN is used, but any other digital pin can be choosen.
/// </summary>
class FirmataStatusLed : public FirmataFeature
{
private:
    int _pin;
	int _triggerCount;
	bool _isOn;
	// These have microsecond resolution
	uint32_t _lastTime;
	uint32_t _minTime;
	
public:
	FirmataStatusLed(int pinNumber)
	{
        _pin = pinNumber;
		Init();
	}

	/// <summary>
	/// Use the default builtin LED for the status. If not defined, uses pin 13
	/// </summary>
	FirmataStatusLed()
	{
#ifdef VERSION_BLINK_PIN
		_pin = VERSION_BLINK_PIN;
#else
		_pin = 13;
#endif
		Init();
	}

	// These have no implementation here - this component has no controllable parts
    virtual void handleCapability(byte pin)
	{
	}
    virtual boolean handlePinMode(byte pin, int mode)
	{
		return false;
	}
	virtual boolean handleSysex(byte command, byte argc, byte* argv) override;
	
    virtual void reset()
    {
		Init();
    }

	virtual void report(bool elapsed) override;

private:
	void Init();
};

#endif

