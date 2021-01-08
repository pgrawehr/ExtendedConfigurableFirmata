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
	static bool ExecuteHardwareAccess(ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result);
};


#endif

