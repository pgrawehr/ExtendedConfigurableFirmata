#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include "ObjectVector.h"
#include "Variable.h"

/// <summary>
/// A stack that can hold arbitrarily sized variables and auto-extends if needed
/// This stack grows from smaller to larger addresses, basically working like a linked list
/// </summary>
class VariableDynamicStack
{
private:
	uint32_t _bytesAllocated;
	Variable* _begin; // bottom of stack
	Variable* _sp; // the stack pointer (points to next element that will be used)
	int* _revPtr; // points to the tail of the stack. This field contains the size of the previous element. It is always 4 bytes in front of _sp
public:
	VariableDynamicStack(int initialElements)
	{
		_bytesAllocated = (initialElements * sizeof(Variable)) + sizeof(void*);
		_begin = (Variable*)malloc(_bytesAllocated);
		memset(_begin, 0, _bytesAllocated);
		_revPtr = (int*)AddBytes(_begin, -4); // Points before start, but won't be used until at least one element is on the stack
		_sp = _begin;
	}

	~VariableDynamicStack()
	{
		if (_begin != nullptr)
		{
			free(_begin);
		}
		_begin = nullptr;
		_sp = nullptr;
		_revPtr = nullptr;
	}

	bool empty() const
	{
		return (_sp == _begin);
	}

	uint32_t BytesUsed() const
	{
		return ByteDifference(_sp, _begin);
	}

	uint32_t FreeBytes() const
	{
		return _bytesAllocated - BytesUsed();
	}

	void push(const Variable& object)
	{
		// This might be 8, depending on compiler alignment of i64 and double types, therefore it is different from sizeof(VariableDescription)
		const int variableDescriptionSizeUsed = Variable::headersize();
		uint32_t sizeUsed = MAX(object.fieldSize(), 8) + variableDescriptionSizeUsed + 4; // + 4 for the tail (the reverse pointer)
		if (sizeUsed > FreeBytes())
		{
			uint32_t newSize = _bytesAllocated + sizeUsed; // Extend so that it certainly matches
			Variable* newBegin = (Variable*)realloc(_begin, newSize); // with this, also _sp and _revPtr become invalid
			int oldOffset = BytesUsed();
			_sp = AddBytes(newBegin, oldOffset); // be sure to calculate in bytes
			_revPtr = (int*)AddBytes(newBegin, oldOffset - 4);
			_bytesAllocated = newSize;
			_begin = newBegin;
		}

		// Now we have enough room for the new variable
		_sp->Size = object.Size; // Tell the operator that it is fine to copy the stuff here
		*_sp = object; // call operator =
		Variable* oldsp = _sp;
		_sp = AddBytes(_sp, sizeUsed);
		_revPtr = (int*)AddBytes(_sp, -4);
		*_revPtr = ByteDifference(_sp, oldsp);
	}

	Variable& top() const
	{
		if (empty())
		{
			Firmata.sendString(F("FATAL: Execution stack underflow"));
		}
		Variable* lastElem = AddBytes(_sp, -*_revPtr);
		return *lastElem;
	}

	/// <summary>
	/// Removes the last element from the stack.
	/// Any previously retrieved elements (using top() or nth() stay valid until the next push)
	/// </summary>
	void pop()
	{
		if (empty())
		{
			Firmata.sendString(F("FATAL: Execution stack underflow"));
		}
		Variable* lastElem = AddBytes(_sp, -*_revPtr);
		_sp = lastElem;
		_revPtr = (int*)AddBytes(_sp, -4);
	}

	// Returns the nth-last element from the stack (0 being the top)
	Variable& nth(int index)
	{
		int i = index;
		Variable* iter = _sp;
		int* rev = _revPtr;
		// One iteration for index == 0
		while (i >= 0)
		{
			iter = AddBytes(iter, -*rev);
			rev = (int*)AddBytes(iter, -4);
			i--;
		}

		return *iter;
	}
};
