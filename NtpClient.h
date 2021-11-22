// SntpSupport.h

#ifndef _NtpClient_h
#define _NtpClient_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

class NtpClient
{
public:
	NtpClient();

	void StartTimeSync(int blinkPin = -1);
};
#endif

