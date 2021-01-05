#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include "ObjectVector.h"

struct VariableListEntry
{
public:
	VariableListEntry()
	{
		Token = 0;
		Next = nullptr;
	}

	int Token;

	VariableListEntry* Next;

	Variable Data; // Stored inline. Length is variable (may reach beyond end of struct!)
};

/// <summary>
/// A List that can contain arbitrary sized variables. Access is O(n)
/// </summary>
class VariableList
{
private:
	VariableListEntry* _first;
	VariableListEntry* _tail;
public:
	typedef VariableListEntry* iterator;

	VariableList()
	{
		_first = nullptr;
		_tail = nullptr;
	}

	~VariableList()
	{
		VariableListEntry* current = _first;;
		while (current != nullptr)
		{
			VariableListEntry* last = current;
			current = last->Next;
			free(last);
		}
		
		_first = nullptr;
		_tail = nullptr;
	}

	Variable& at(int token)
	{
		VariableListEntry* current = _first;;
		while (current != nullptr)
		{
			if (current->Token == token)
			{
				return current->Data;
			}
			
			current = current->Next;
		}

		_first = nullptr;
		return _first->Data; // Boom! todo: Throw
	}

	bool contains(int token) const
	{
		VariableListEntry* current = _first;;
		while (current != nullptr)
		{
			if (current->Token == token)
			{
				return true;
			}

			current = current->Next;
		}
		
		return false;
	}

	/// <summary>
	/// Appends a new entry to the list. Only use when the token is not already there.
	/// The entry's data part is ignored in this method, but the full size is reserved.
	/// </summary>
	/// <param name="token">Token of the new entry</param>
	/// <param name="entry">Description of the new entry</param>
	/// <returns>Reference to the new entry</returns>
	Variable& insert(int token, Variable& entry)
	{
		// Not found. Create new entry
		// TODO: Should know how much we need to store the variable header alone
		size_t size = sizeof(Variable) + entry.fieldSize() + sizeof(int) + sizeof(void*);
		VariableListEntry* newMem = (VariableListEntry*)malloc(size);
		memset(newMem, 0, size);
		newMem->Token = token;
		newMem->Next = nullptr;
		if (_tail == nullptr)
		{
			// First entry in queue
			_first = _tail = newMem;
		}
		else
		{
			_tail->Next = newMem;
			_tail = newMem;
		}

		return newMem->Data;
	}

	Variable& insertOrUpdate(int token, Variable& entry)
	{
		VariableListEntry* current = _first;;
		while (current != nullptr)
		{
			if (current->Token == token)
			{
				// Check size is equal. There's something seriously wrong if not
				if (current->Data.fieldSize() != entry.fieldSize())
				{
					return entry; // todo: Throw. This must not happen
				}
				// Overwrite existing content
				memcpy(&current->Data.Int32, &entry.Int32, entry.fieldSize());
				return current->Data;
			}

			current = current->Next;
		}

		// Not found. Create new entry
		// TODO: Should know how much we need to store the variable header alone
		size_t size = sizeof(Variable) + entry.fieldSize() + sizeof(int) + sizeof(void*);
		VariableListEntry* newMem = (VariableListEntry*)malloc(size);
		memset(newMem, 0, size);
		newMem->Data = entry;// performs a full copy
		newMem->Token = token;
		newMem->Next = nullptr;
		if (_tail == nullptr)
		{
			// First entry in queue
			_first = _tail = newMem;
		}
		else
		{
			_tail->Next = newMem;
			_tail = newMem;
		}

		return newMem->Data;
	}
};
