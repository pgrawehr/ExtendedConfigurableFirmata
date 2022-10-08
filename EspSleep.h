// EspSleep.h

#pragma once
#include <ConfigurableFirmata.h>
#include "FirmataFeature.h"

class EspSleep : public FirmataFeature
{
private:
	bool _goToSleepAfterDisconnect;
	int _sleepTimeout;
	ulong _lastDisconnect;
	short _wakeupPin;
	byte _triggerValue;

public:
	EspSleep(short wakeupPin, byte triggerValue)
		: FirmataFeature()
	{
		_wakeupPin = wakeupPin;
		_triggerValue = triggerValue;
		_goToSleepAfterDisconnect = false;
		_sleepTimeout = 0;
		_lastDisconnect = 0;
	}

	virtual boolean handleSysex(byte command, byte argc, byte* argv) override;

	void reset() override;

	void Update(bool isCurrentlyConnected);

	void handleCapability(byte pin) override;

	boolean handlePinMode(byte pin, int mode) override;

private:
	void EnterSleepMode();
};
