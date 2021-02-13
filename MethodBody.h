#pragma once


#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"
#include "NativeMethod.h"

class MethodBody
{
public:
	MethodBody(byte flags, byte numArgs, byte maxStack);

	virtual ~MethodBody();

private:
	/// <summary>
	/// Clear the current entry, so it can be reused.
	/// </summary>
	void Clear();

public:
	byte NumberOfArguments() const
	{
		return _numArgs;
	}

	byte MaxExecutionStack() const
	{
		return _maxStack;
	}

	byte MethodFlags() const
	{
		return _methodFlags;
	}

	stdSimple::complexIteratorBase<VariableDescription>& GetLocalsIterator() const;
	stdSimple::complexIteratorBase<VariableDescription>& GetArgumentTypesIterator() const;

	int32_t methodToken; // Primary method token (a methodDef token)
	u16 methodLength;
	u16 codeReference;

	stdSimple::vector<VariableDescription, byte> localTypes;
	stdSimple::vector<VariableDescription, byte> argumentTypes;
	byte* methodIl;
	// Native method number
	NativeMethod nativeMethod;

private:
	byte _numArgs;
	byte _maxStack;
	byte _methodFlags;
};

class SortedMethodList : public SortedList<MethodBody>
{
public:
	void CopyToFlash() override;
	void ThrowNotFoundException(int token) override;
	void clear(bool includingFlash) override;
};
