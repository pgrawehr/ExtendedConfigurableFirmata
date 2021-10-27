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

#define STATUS_IDLE 0
#define STATUS_COMMANDED 1
#define STATUS_LOADING_PROGRAM 2
#define STATUS_EXECUTING_PROGRAM 3
#define STATUS_ERROR 4

#define STATUS_NUMBER_OF_STEPS 4


/// <summary>
/// Simple component that reports the CPU status by some blinking patterns.
/// By default, the VERSION_BLINK_PIN is used, but any other digital pin can be choosen.
/// </summary>
class FirmataStatusLed : public FirmataFeature
{
private:
    int _pin;
	int _status;
	uint32_t _startClock;
	uint32_t _resetAt;
	bool _isOn;
	
public:
	static FirmataStatusLed* FirmataStatusLedInstance;
	FirmataStatusLed(int pinNumber)
	{
		FirmataStatusLedInstance = this;
        _pin = pinNumber;
		Init();
	}

	/// <summary>
	/// Use the default builtin LED for the status. If not defined, uses pin 13
	/// </summary>
	FirmataStatusLed()
	{
		FirmataStatusLedInstance = this;
#ifdef VERSION_BLINK_PIN
		_pin = VERSION_BLINK_PIN;
#else
		_pin = 13;
#endif
		Init();
	}

	virtual void handleCapability(byte pin)
	{
	}

	virtual boolean handlePinMode(byte pin, int mode)
	{
		setStatus(STATUS_COMMANDED, 500);
		return false;
	}

	virtual boolean handleSysex(byte command, byte argc, byte* argv) override;
	
	virtual void reset()
	{
		Init();
	}

	void setStatus(int status, int resetAfter);

	int getStatus()
	{
		return _status;
	}

	virtual void report(bool elapsed) override;

private:
	void Init();
};

#endif
