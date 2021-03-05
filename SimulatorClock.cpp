#include <ConfigurableFirmata.h>
#include "SimulatorClock.h"

#ifdef SIM

#undef INPUT
#include <Windows.h>


void SimulatorClock::Init()
{
}

int64_t SimulatorClock::ReadTime()
{
	SYSTEMTIME time;
	GetSystemTime(&time);
	return GetAsDateTimeTicks(time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}


#endif
