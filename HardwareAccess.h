// HardwareAccess.h

#ifndef _HARDWAREACCESS_h
#define _HARDWAREACCESS_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "ConfigurableFirmata.h"
#include "FirmataIlExecutor.h"

class HardwareAccess
{
	// These two members keep the current system tick count as a 64 bit variable, internally working around possible overflows.
	// They should run at the highest measureable frequency of a particular system. 
	static int64_t _tickCount64; // Uses the frequency below (1.000.000 for arduinos, with the fastest counter running at microsecond-resolution)
	static int64_t _tickCountFrequency;
	static uint32_t _lastTickCount;
public:
	static void Init();
	static void UpdateClocks(); // Called regularly by the execution engine, to also keep track of overflows if no tickcount methods are being used for some time
	static bool ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable&
	                                  result);
private:
	static int64_t TickCount64();
};


#endif

