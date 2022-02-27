
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
			// We're currently not reusing existing entries. But these handles are rarely used, so this should not normally cause a memory leak
		pair<void*, void*> newElem(args[0].Object, args[1].Object);
		int offset = executor->_weakDependencies.push_back(newElem);
		result.Type = VariableKind::Int32;
		result.setSize(4),
		result.Int32 = offset + 1; // because 0 is an invalid handle
		}
		break;
	case NativeMethod::DependentHandle_InternalFree:
	{
		ASSERT(args.size() == 1);
		size_t handle = args[0].Uint32;
		if (handle >= executor->_weakDependencies.size())
		{
			break;
		}
		auto& elem = executor->_weakDependencies.at(handle);
		elem.first = nullptr;
		elem.second = nullptr;
		break;
	}
	default:
		return false;
	}

	return true;
}

