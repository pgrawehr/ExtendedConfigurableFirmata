// Esp32Rtc.h

#ifndef _ESP32RTC_h
#define _ESP32RTC_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif
#include "RtcBase.h"

class Esp32Rtc : public RtcBase
{
private:
	int64_t _previousValue;
	int32_t _previousMs;
public:
	Esp32Rtc()
	{
		_previousValue = 0x48d8d5bb26a9dc17; // 2020-02-20 16:18 UTC;
	}
	virtual void Init() override;

	int64_t ReadTime() override;
};


#endif

