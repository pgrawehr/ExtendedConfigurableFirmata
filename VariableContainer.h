#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include "ObjectVector.h"

/// <summary>
/// A vector that can contain values of different sizes. The number of items in the vector is fixed, though
/// </summary>
class VariableVector
{
private:
	size_t _size;
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

	void InitDefault(int size)
	{
		_defaultSizesOnly = true;
		if (_data != nullptr)
		{
			free(_data);
		}

		if (size > 0)
		{
			_data = (Variable*)malloc(size * sizeof(Variable));
			memset(_data, 0, size * sizeof(Variable));
		}
		else
		{
			_data = nullptr;
		}
		_size = size;
	}

	void InitFrom(stdSimple::vector<VariableDescription> &variableDescriptions)
	{
		bool canUseDefaultSizes = true;
		int totalSize = 0;
		// Check whether the list contains any large value type objects. If not, we will be using constant field sizes of sizeof(Variable)
		// In the same run, sum up all sizes
		for (auto start = variableDescriptions.begin(); start != variableDescriptions.end(); ++start)
		{
			size_t size = start->fieldSize();
			if (size > sizeof(double))
			{
				canUseDefaultSizes = false;
				break;
			}
			totalSize += MIN(size, sizeof(double)) + 4; // Each variable carries a 4 byte header and holds at least 8 bytes
		}

		if (canUseDefaultSizes)
		{
			InitDefault(variableDescriptions.size());
			return;
		}

		// This variable still contains the number of elements in the vector
		_size = variableDescriptions.size();
		_defaultSizesOnly = false;
		totalSize += sizeof(VariableDescription);
		_data = (Variable*)malloc(totalSize);
		memset(_data, 0, totalSize);

		Variable* currentField = _data;
		byte* currentFieldPtr = (byte*)_data;
		// Now init the data structure: It's a ordered list with "next" pointers in the form of the size fields
		for (auto start = variableDescriptions.begin(); start != variableDescriptions.end(); ++start)
		{
			size_t size = start->fieldSize();
			currentField->Type = start->Type;
			currentField->Marker = start->Marker;
			currentField->Size = MIN(start->Size, 8);
			currentFieldPtr += sizeof(VariableDescription) + currentField->Size;
			currentField = (Variable*)currentFieldPtr;
		}
		// There's room for one last VariableDescription after the now filled data. Leave it empty, so we have a guard when traversing the list.
	}

	~VariableVector()
	{
		if (_data != nullptr)
		{
			free(_data);
		}
		_data = nullptr;
	}

	Variable& at(size_t index)
	{
		if (_defaultSizesOnly)
		{
			return _data[index];
		}
		else
		{
			Variable* variablePtr = _data;
			byte* bytePtr = (byte*)_data;
			size_t currentIndex = 0;
			while(currentIndex < index && variablePtr->Marker != 0)
			{
				bytePtr += variablePtr->Size + 4;
				variablePtr = (Variable*)bytePtr;
				currentIndex++;
			}

			if (variablePtr->Marker == 0)
			{
				// This will blow, stopping the program. We should never get here (means the index was out of bounds)
				variablePtr = nullptr;
			}

			// Return a reference to the variable we've found
			return *variablePtr;
		}
	}

	Variable& at(const size_t index) const
	{
		if (_defaultSizesOnly)
		{
			return _data[index];
		}
		else
		{
			Variable* variablePtr = _data;
			byte* bytePtr = (byte*)_data;
			size_t currentIndex = 0;
			while (currentIndex < index && variablePtr->Marker != 0)
			{
				bytePtr += variablePtr->Size + 4;
				variablePtr = (Variable*)bytePtr;
				currentIndex++;
			}

			if (variablePtr->Marker == 0)
			{
				// This will blow, stopping the program. We should never get here (means the index was out of bounds)
				variablePtr = nullptr;
			}

			// Return a reference to the variable we've found
			return *variablePtr;
		}
	}

	size_t size() const
	{
		return _size;
	}

	Variable& operator[] (size_t index)
	{
		return at(index);
	}

	Variable& operator[] (const size_t index) const
	{
		return at(index);
	}
};
