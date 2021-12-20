
#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "DependentHandle.h"

#include "SelfTest.h"

void DependentHandle::Init()
{
}

DependentHandle::DependentHandle()
{
}

bool DependentHandle::ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	switch(method)
	{
	case NativeMethod::DependentHandle_InternalInitialize:
		{
		ASSERT(args.size() == 2);
		pair<void*, void*> newElem(args[0].Object, args[1].Object);
		int offset = executor->_weakDependencies.push_back(newElem);
		result.Type = VariableKind::Int32;
		result.setSize(4),
		result.Int32 = offset + 1; // because 0 is an invalid handle
		}
		break;
	default:
		return false;
	}

	return true;
}

