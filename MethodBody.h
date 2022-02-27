#pragma once


#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"
#include "interface/NativeMethod.h"
#include "interface/ExceptionHandlingClauseOptions.h"

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

// One exception clause. A method can have 0 or many of these.
class ExceptionClause
{
private:
	int32_t _methodToken;
public:
	ExceptionClause(int token)
	{
		_methodToken = token;
		FilterToken = 0;
		ClauseType = ExceptionHandlingClauseOptions::Clause;
		TryOffset = TryLength = HandlerLength = HandlerOffset = 0;
	}

	uint32_t GetKey()
	{
		return _methodToken;
	}
	ExceptionHandlingClauseOptions ClauseType;
	uint16_t TryOffset;
	uint16_t TryLength;
	uint16_t HandlerOffset;
	uint16_t HandlerLength;
	int FilterToken; // The token of the exception type in this catch clause
};

class SortedClauseList : public SortedList<ExceptionClause>
{

public:
	void CopyContentsToFlash(FlashMemoryManager* manager) override;
	void ThrowNotFoundException(int token) override;
	void ValidateListOrder() override;
	ExceptionClause* BinarySearchKeyInternal(stdSimple::vector<ExceptionClause*>* list, uint32_t key, uint32_t& index) override;

	void clear(bool includingFlash) override;
private:
	ExceptionClause* CreateFlashDeclaration(FlashMemoryManager* manager, ExceptionClause* element);
};
