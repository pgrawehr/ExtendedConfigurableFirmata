
#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"
#include "MethodBody.h"
#include "Exceptions.h"
#include "FlashMemoryManager.h"
#include "SystemException.h"

MethodBody::MethodBody(byte flags, byte numArgs, byte maxStack)
{
	methodToken = 0;
	_methodFlags = flags;
	methodLength = 0;
	methodIl = nullptr;
	_numArgs = numArgs;
	_maxStack = maxStack;
	codeReference = -1;
	nativeMethod = NativeMethod::None;
}

MethodBody::~MethodBody()
{
	Clear();
}

void MethodBody::Clear()
{
	methodToken = 0;
	if (methodIl != nullptr)
	{
		free(methodIl);
		methodIl = nullptr;
		methodLength = 0;
	}

	codeReference = -1;
}

MethodBodyDynamic::MethodBodyDynamic(byte flags, byte numArgs, byte maxStack)
	:MethodBody(flags, numArgs, maxStack)
{
}

VariableDescription& MethodBodyDynamic::GetArgumentAt(int idx) const
{
	return _argumentTypes.at(idx);
}

void MethodBodyDynamic::Clear()
{
	_localTypes.clear(true);
	_argumentTypes.clear(true);
}


stdSimple::complexIteratorBase<VariableDescription>& MethodBodyDynamic::GetLocalsIterator() const
{
	// We keep one instance of this iterator as a static variable, so we can return it in a fire-and-forget way
	static stdSimple::vector<VariableDescription, byte>::complexVectorIterator iteratorInUse;
	iteratorInUse = _localTypes.GetIterator();
	return iteratorInUse;
}

stdSimple::complexIteratorBase<VariableDescription>& MethodBodyDynamic::GetArgumentTypesIterator() const
{
	// We keep one instance of this iterator as a static variable, so we can return it in a fire-and-forget way
	static stdSimple::vector<VariableDescription, byte>::complexVectorIterator iteratorInUse;
	iteratorInUse = _argumentTypes.GetIterator();
	return iteratorInUse;
}

void SortedMethodList::CopyToFlash()
{
	throw stdSimple::ExecutionEngineException("Not supported stuff here");
}

void SortedMethodList::ThrowNotFoundException(int token)
{
	throw stdSimple::ClrException(SystemException::MissingMethod, token);
}

void SortedMethodList::clear(bool includingFlash)
{
	if (includingFlash)
	{
		_flashEntries.clear();
		FlashManager.Clear();
	}

	for (size_t i = 0; i < _ramEntries.size(); i++)
	{
		delete _ramEntries[i];
	}

	_ramEntries.clear(true);
}

