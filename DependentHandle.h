#include <ConfigurableFirmata.h>
#include "HardwareAccess.h"

#pragma once
class DependentHandle : public LowlevelInterface
{
public:
	DependentHandle();
	void Init() override;
	void Update() override
	{
		// Nothing to do for now
	}

	bool ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method,
		const VariableVector& args, Variable& result) override;
};
