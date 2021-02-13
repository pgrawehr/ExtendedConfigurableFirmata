#pragma once


#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"
#include "NativeMethod.h"

class MethodBody
{
public:
	MethodBody(byte flags, byte numArgs, byte maxStack);

	virtual ~MethodBody();

protected:
	/// <summary>
	/// Clear the current entry, so it can be reused.
	/// </summary>
	virtual void Clear();

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

	virtual stdSimple::complexIteratorBase<VariableDescription>& GetLocalsIterator() const = 0;
	virtual stdSimple::complexIteratorBase<VariableDescription>& GetArgumentTypesIterator() const = 0;

	virtual VariableDescription& GetArgumentAt(int idx) const = 0;

	virtual bool IsDynamic() const = 0;

	int32_t methodToken; // Primary method token (a methodDef token)
	u16 methodLength;
	u16 codeReference;

	byte* methodIl;
	// Native method number
	NativeMethod nativeMethod;

private:
	byte _numArgs;
	byte _maxStack;
	byte _methodFlags;
};

class MethodBodyDynamic : public MethodBody
{
private:
	stdSimple::vector<VariableDescription, byte> _localTypes;
	stdSimple::vector<VariableDescription, byte> _argumentTypes;
public:
	MethodBodyDynamic(byte flags, byte numArgs, byte maxStack);
	virtual stdSimple::complexIteratorBase<VariableDescription>& GetLocalsIterator() const override;
	virtual stdSimple::complexIteratorBase<VariableDescription>& GetArgumentTypesIterator() const override;
	void AddLocalDescription(VariableDescription& desc)
	{
		_localTypes.push_back(desc);
	}

	void AddArgumentDescription(VariableDescription& desc)
	{
		_localTypes.push_back(desc);
	}

	bool IsDynamic() const override
	{
		return true;
	}

	VariableDescription& GetArgumentAt(int idx) const override;

protected:
	void Clear() override;
};

class SortedMethodList : public SortedList<MethodBody>
{
public:
	void CopyToFlash() override;
	void ThrowNotFoundException(int token) override;
	void clear(bool includingFlash) override;
};
