#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include "MemoryManagement.h"
#include "ObjectVector.h"
#include "Exceptions.h"

/// <summary>
/// A vector that can contain values of different sizes. The number of items in the vector is fixed, though
/// </summary>
class VariableVector
{
private:
	int _size;
	// True if all elements within the vector are sizeof(Variable)
	bool _defaultSizesOnly;
	Variable* _data;
public:
	typedef Variable* iterator;

	VariableVector()
	{
		_data = nullptr;
		_defaultSizesOnly = true;
		_size = 0;
	}

	bool InitDefault(int numDescriptions, VariableDescription* variableDescriptions)
	{
		_defaultSizesOnly = true;
		if (_data != nullptr)
		{
			freeEx(_data);
			_data = nullptr;
		}

		if (numDescriptions > 0)
		{
			_data = (Variable*)mallocEx(numDescriptions * sizeof(Variable));
			if (_data == nullptr)
			{
				stdSimple::OutOfMemoryException::Throw("Out of memory initializing default variable description list");
				return false;
			}
			
			memset(_data, 0, numDescriptions * sizeof(Variable));

			for (int i = 0; i < numDescriptions; i++)
			{
				VariableDescription* start = variableDescriptions + i;
				size_t fieldSize = start->fieldSize();
				_data[i].setSize((uint16_t)fieldSize);
				_data[i].Type = start->Type;
				_data[i].Marker = start->Marker;
			}
		}
		else
		{
			_data = nullptr;
		}
		
		_size = numDescriptions;
		return true;
	}

	bool InitFrom(int numDescriptions, VariableDescription* variableDescriptions)
	{
		bool canUseDefaultSizes = true;
		int totalSize = 0;
		// Check whether the list contains any large value type objects. If not, we will be using constant field sizes of sizeof(Variable)
		// In the same run, sum up all sizes
		// Firmata.sendStringf(F("Initfrom: %d descriptions, address 0x%x"), 8, numDescriptions, variableDescriptions);
		for (int i = 0; i < numDescriptions; i++)
		{
			size_t size = variableDescriptions[i].fieldSize();
			if (size > sizeof(double))
			{
				canUseDefaultSizes = false;
			}
			totalSize += MAX(size, sizeof(double)) + Variable::headersize(); // Each variable carries a header and holds at least 8 bytes
		}

		if (canUseDefaultSizes)
		{
			return InitDefault(numDescriptions, variableDescriptions);
		}

		// This variable contains the number of elements in the vector, even if the vector has variable-lenght entries
		_size = numDescriptions;
		_defaultSizesOnly = false;
		totalSize += sizeof(VariableDescription);
		_data = (Variable*)mallocEx(totalSize);
		if (_data == nullptr)
		{
			stdSimple::OutOfMemoryException::Throw("Out of memory initalizing dynamic variable vector");
			return false;
		}
		
		memset(_data, 0, totalSize);

		Variable* currentField = _data;
		byte* currentFieldPtr = (byte*)_data;
		// Now init the data structure: It's a ordered list with "next" pointers in the form of the size fields
		for (int i = 0; i < numDescriptions; i++)
		{
			VariableDescription* start = variableDescriptions + i;
			size_t size = MAX(start->fieldSize(), 8);
			currentField->Type = start->Type;
			currentField->Marker = start->Marker;
			currentField->setSize((uint16_t)size);
			currentFieldPtr += Variable::headersize() + size;
			currentField = (Variable*)currentFieldPtr;
		}
		// There's room for one last VariableDescription after the now filled data. Leave it empty, so we have a guard when traversing the list.
		return true;
	}

	~VariableVector()
	{
		if (_data != nullptr)
		{
			freeEx(_data);
		}
		_data = nullptr;
	}

	Variable& at(int index) const
	{
		if (_defaultSizesOnly)
		{
			return _data[index];
		}
		else
		{
			Variable* variablePtr = _data;
			byte* bytePtr = (byte*)_data;
			int currentIndex = 0;
			while(currentIndex < index && variablePtr->Marker != 0)
			{
				bytePtr += MAX(variablePtr->fieldSize(), 8) + Variable::headersize();
				variablePtr = (Variable*)bytePtr;
				currentIndex++;
			}

			if (variablePtr->Marker == 0)
			{
				// This will blow, stopping the program. We should never get here (means the index was out of bounds)
				throw stdSimple::ExecutionEngineException("Variable vector subscript out of range");
			}

			// Return a reference to the variable we've found
			return *variablePtr;
		}
	}

	int size() const
	{
		return _size;
	}

	Variable& operator[] (int index)
	{
		return at(index);
	}

	Variable& operator[] (const int index) const
	{
		return at(index);
	}
};
