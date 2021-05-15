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
		return _numArguments;
	}

	byte MaxExecutionStack() const
	{
		return _maxStack;
	}

	byte MethodFlags() const
	{
		return _methodFlags;
	}

	uint16_t MethodLength() const
	{
		if (_methodIl == nullptr)
		{
			return 0;
		}

		return _methodLength;
	}

	NativeMethod NativeMethodNumber() const
	{
		if (_methodIl != nullptr)
		{
			return NativeMethod::None;
		}

		return _nativeMethod;
	}

	virtual short NumberOfLocals() const = 0;

	virtual VariableDescription* GetLocalsIterator() const = 0;
	virtual VariableDescription* GetArgumentTypesIterator() const = 0;

	virtual VariableDescription& GetArgumentAt(int idx) const = 0;

	virtual bool IsDynamic() const = 0;

	int32_t methodToken; // Primary method token (a methodDef token)

	byte* _methodIl;

	uint32_t GetKey() const
	{
		return methodToken;
	}

	union
	{
		// We need only either of these two
		NativeMethod _nativeMethod; // Native method number
		uint16_t _methodLength;
	};
	
protected:
	
	byte _numArguments;
	byte _maxStack;
	byte _methodFlags;
};

class MethodBodyDynamic : public MethodBody
{
	friend class SortedMethodList;
private:
	stdSimple::vector<VariableDescription, byte> _localTypes;
	stdSimple::vector<VariableDescription, byte> _argumentTypes;
public:
	MethodBodyDynamic(byte flags, byte numArgs, byte maxStack);
	virtual VariableDescription* GetLocalsIterator() const override;
	virtual VariableDescription* GetArgumentTypesIterator() const override;
	void AddLocalDescription(VariableDescription& desc)
	{
		_localTypes.push_back(desc);
	}

	void AddArgumentDescription(VariableDescription& desc)
	{
		_argumentTypes.push_back(desc);
	}

	bool IsDynamic() const override
	{
		return true;
	}

	virtual short NumberOfLocals() const override
	{
		return (short)_localTypes.size();
	}

	VariableDescription& GetArgumentAt(int idx) const override;

protected:
	void Clear() override;
};

class MethodBodyFlash : public MethodBody
{
	friend class SortedMethodList;
private:
	VariableDescription* _locals;
	VariableDescription* _arguments;
	short _numLocals;
	MethodBodyFlash(MethodBodyDynamic* from);
public:
	bool IsDynamic() const override
	{
		return false;
	}

	void Clear() override
	{
		// This is in flash -> cannot delete
	}

	VariableDescription& GetArgumentAt(int idx) const override;

	virtual VariableDescription* GetLocalsIterator() const override;
	virtual VariableDescription* GetArgumentTypesIterator() const override;

	virtual short NumberOfLocals() const override
	{
		return _numLocals;
	}
};

class SortedMethodList : public SortedList<MethodBody>
{
public:
	void CopyContentsToFlash(FlashMemoryManager* manager) override;
	void ThrowNotFoundException(int token) override;

	void clear(bool includingFlash) override;
private:
	MethodBodyFlash* CreateFlashDeclaration(FlashMemoryManager* manager, MethodBodyDynamic* element);
};
