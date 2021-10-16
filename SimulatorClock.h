// SimulatorClock.h

#ifndef _SIMULATORCLOCK_h
#define _SIMULATORCLOCK_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif
#include "RtcBase.h"

class SimulatorClock : public RtcBase
{
public:
	void Init() override;

	int64_t ReadTime() override;
};

#endif
