#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>

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
		}
		else
		{
			_data = nullptr;
		}
		_size = size;
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
		return _data[index];
	}

	Variable& at(const size_t index) const
	{
		return _data[index];
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

	iterator begin() const
	{
		return _data;
	}

	iterator end() const
	{
		return _data + _size;
	}
};
