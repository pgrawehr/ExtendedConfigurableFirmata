// Ds1307.h

#ifndef _DS1307_h
#define _DS1307_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "RtcBase.h"

class Ds1307 : public RtcBase
{
private:
	const int DS1307_ADDRESS = 0x68;
	int64_t _previousValue;
public:
	Ds1307()
	{
		_previousValue = 0x48d8d5bb26a9dc17; // 2020-02-20 16:18 UTC;
	}
	virtual void Init() override;

	int64_t ReadTime();
};

#endif
