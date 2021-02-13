﻿
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
	_numArguments = numArgs;
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


void SortedMethodList::CopyToFlash()
{
	for (auto iterator = _ramEntries.begin(); iterator != _ramEntries.end(); ++iterator)
	{
		MethodBodyFlash* flash = CreateFlashDeclaration((MethodBodyDynamic*)*iterator);
		_flashEntries.push_back(flash);
	}

	clear(false);
}

MethodBodyFlash* SortedMethodList::CreateFlashDeclaration(MethodBodyDynamic* dynamic)
{
	// First create the object in RAM
	int totalSize = sizeof(MethodBodyFlash) + dynamic->_argumentTypes.size() * sizeof(VariableDescription) + dynamic->_localTypes.size() * sizeof(VariableDescription) + dynamic->methodLength;

	byte* flashCopy = (byte*)malloc(totalSize);
	byte* flashTarget = (byte*)FlashManager.FlashAlloc(totalSize);

	byte* temp = flashCopy;
	// Reserve space for main class
	temp = AddBytes(temp, sizeof(MethodBodyFlash));

	MethodBodyFlash* flash = new MethodBodyFlash(dynamic);
	flash->codeReference = dynamic->codeReference;
	flash->nativeMethod = dynamic->nativeMethod;
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

	if (dynamic->methodLength > 0)
	{
		memcpy(temp, dynamic->methodIl, dynamic->methodLength);
		flash->methodIl = (byte*)Relocate(flashCopy, temp, flashTarget);
		flash->methodLength = dynamic->methodLength;
		temp = AddBytes(temp, dynamic->methodLength);
	}
	else
	{
		flash->methodLength = 0;
		flash->methodIl = nullptr;
	}
	
	memcpy(flashCopy, (void*)flash, sizeof(MethodBodyFlash));
	
	FlashManager.CopyToFlash(flashCopy, flashTarget, totalSize);
	flash->methodIl = nullptr; // Because the delete shall not touch this
	delete flash;
	free(flashCopy);

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
		FlashManager.Clear();
	}

	for (size_t i = 0; i < _ramEntries.size(); i++)
	{
		delete _ramEntries[i];
	}

	_ramEntries.clear(true);
}

