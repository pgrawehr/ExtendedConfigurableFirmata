// HardwareAccess.h

#ifndef _HARDWAREACCESS_h
#define _HARDWAREACCESS_h


#include "ConfigurableFirmata.h"
#include "FirmataIlExecutor.h"

class LowlevelInterface
{
public:
	virtual void Init() = 0;
	virtual void Update() = 0;
	virtual bool ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable&
		result) = 0;
	virtual ~LowlevelInterface()
	{
	}
};

class HardwareAccess : public LowlevelInterface
{
	// These two members keep the current system tick count as a 64 bit variable, internally working around possible overflows.
	// They should run at the highest measureable frequency of a particular system. 
	static int64_t _tickCount64; // Uses the frequency below (1.000.000 for arduinos, with the fastest counter running at microsecond-resolution)
	static int64_t _tickCountFrequency;
	static uint32_t _lastTickCount;
public:
	void Init() override;
	void Update() override; // Called regularly by the execution engine, to also keep track of overflows if no tickcount methods are being used for some time
	bool ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable&
	                                  result) override;

	static void Reboot();
private:
	static int64_t TickCount64();
};


#endif
