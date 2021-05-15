
#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"
#include "MethodBody.h"
#include "Exceptions.h"
#include "FlashMemoryManager.h"
#include "SystemException.h"

MethodBody::MethodBody(byte flags, byte numArgs, byte maxStack)
{
	methodToken = 0;
	_nativeMethod = NativeMethod::None;
	_methodFlags = flags;
	_methodLength = 0;
	_methodIl = nullptr;
	_numArguments = numArgs;
	_maxStack = maxStack;
}

MethodBody::~MethodBody()
{
	Clear();
}

void MethodBody::Clear()
{
	methodToken = 0;
	if (_methodIl != nullptr)
	{
		freeEx(_methodIl);
		_methodIl = nullptr;
		_methodLength = 0;
	}
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


VariableDescription* MethodBodyDynamic::GetLocalsIterator() const
{
	return &_localTypes.at(0);
}

VariableDescription* MethodBodyDynamic::GetArgumentTypesIterator() const
{
	return &_argumentTypes.at(0);
}

MethodBodyFlash::MethodBodyFlash(MethodBodyDynamic* from)
	: MethodBody(from->MethodFlags(), from->NumberOfArguments(), from->MaxExecutionStack())
{
	_locals = nullptr;
	_numLocals = 0;
	_arguments = nullptr;
}

VariableDescription& MethodBodyFlash::GetArgumentAt(int idx) const
{
	return _arguments[idx];
}

VariableDescription* MethodBodyFlash::GetArgumentTypesIterator() const
{
	return _arguments;
}

VariableDescription* MethodBodyFlash::GetLocalsIterator() const
{
	return _locals;
}


void SortedMethodList::CopyContentsToFlash(FlashMemoryManager* manager)
{
	for (auto iterator = _ramEntries.begin(); iterator != _ramEntries.end(); ++iterator)
	{
		MethodBodyFlash* flash = CreateFlashDeclaration(manager, (MethodBodyDynamic*)*iterator);
		_flashEntries.push_back(flash);
	}

	clear(false);
}

MethodBodyFlash* SortedMethodList::CreateFlashDeclaration(FlashMemoryManager* manager, MethodBodyDynamic* dynamic)
{
	// First create the object in RAM
	int totalSize = sizeof(MethodBodyFlash) + dynamic->_argumentTypes.size() * sizeof(VariableDescription) + dynamic->_localTypes.size() * sizeof(VariableDescription) + dynamic->MethodLength();

	byte* flashCopy = (byte*)mallocEx(totalSize);
	byte* flashTarget = (byte*)manager->FlashAlloc(totalSize);

	byte* temp = flashCopy;
	// Reserve space for main class
	temp = AddBytes(temp, sizeof(MethodBodyFlash));

	MethodBodyFlash* flash = new MethodBodyFlash(dynamic);
	if (dynamic->NativeMethodNumber() != NativeMethod::None)
	{
		flash->_nativeMethod = dynamic->NativeMethodNumber();
	}
	
	flash->methodToken = dynamic->methodToken;
	
	size_t argumentLength = dynamic->_argumentTypes.size() * sizeof(VariableDescription);
	if (argumentLength > 0)
	{
		memcpy(temp, &dynamic->_argumentTypes.at(0), argumentLength);
		flash->_arguments = (VariableDescription*)Relocate(flashCopy, temp, flashTarget);
		temp = AddBytes(temp, argumentLength);
		flash->_numArguments = (byte)dynamic->_argumentTypes.size();
	}
	else
	{
		flash->_arguments = nullptr;
	}

	size_t localsLength = dynamic->_localTypes.size() * sizeof(VariableDescription);
	if (localsLength > 0)
	{
		memcpy(temp, &dynamic->_localTypes.at(0), localsLength);
		flash->_locals = (VariableDescription*)Relocate(flashCopy, temp, flashTarget);
		temp = AddBytes(temp, localsLength);
		flash->_numLocals = (short)dynamic->_localTypes.size();;
	}
	else
	{
		flash->_locals = nullptr;
	}

	if (dynamic->MethodLength() > 0)
	{
		memcpy(temp, dynamic->_methodIl, dynamic->_methodLength);
		flash->_methodIl = (byte*)Relocate(flashCopy, temp, flashTarget);
		flash->_methodLength = dynamic->_methodLength;
		temp = AddBytes(temp, dynamic->_methodLength);
	}
	else
	{
		flash->_methodIl = nullptr;
	}
	
	memcpy(flashCopy, (void*)flash, sizeof(MethodBodyFlash));
	
	manager->CopyToFlash(flashCopy, flashTarget, totalSize);
	flash->_methodIl = nullptr; // Because the delete shall not touch this
	delete flash;
	freeEx(flashCopy);

	return (MethodBodyFlash*)flashTarget;
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
	}

	for (size_t i = 0; i < _ramEntries.size(); i++)
	{
		deleteEx(_ramEntries[i]);
	}

	_ramEntries.clear(true);
}

