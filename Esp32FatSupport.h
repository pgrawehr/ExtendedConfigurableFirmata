// Esp32FatSupport.h

#include <ConfigurableFirmata.h>
#include "HardwareAccess.h"

#pragma once
class Esp32FatSupport : public HardwareAccess
{
public:
	Esp32FatSupport();
	void Init() override;
	void Update() override;
	bool ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method,
		const VariableVector& args, Variable& result) override;
	~Esp32FatSupport() override;
};
