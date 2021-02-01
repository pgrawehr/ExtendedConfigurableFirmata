#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include "Exceptions.h"
#include "VariableKind.h"
#include "openum.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define VARIABLE_DEFAULT_MARKER  0x37
#define VARIABLE_DECLARATION_MARKER 0x39

/// <summary>
/// Pointer arithmetic on byte level on other object types. This shall be used if the offset is in bytes, but T is some other pointer type.
/// </summary>
template <typename T>
T* AddBytes(T* inPtr, int offset)
{
	return (T*)(((byte*)inPtr) + offset);
}

template <typename T>
int ByteDifference(T* higher, T* lower)
{
	return (byte*)higher - (byte*)lower;
}

/// <summary>
/// Relocates a pointer (to compute the same address relative to a new base)
/// </summary>
/// <param name="inBasePtr">Input base pointer</param>
/// <param name="inPtr">Input pointer. Must be &gt; inBasePtr</param>
/// <param name="outBasePtr">New base pointer</param>
template <typename T>
T* Relocate(T* inBasePtr, T* inPtr, T* outBasePtr)
{
	int offset = ByteDifference(inPtr, inBasePtr);
	return AddBytes(outBasePtr, offset);
}

inline VariableKind operator &(VariableKind a, VariableKind b)
{
	return (VariableKind)((int)a & (int)b);
}

inline VariableKind operator ~(VariableKind a)
{
	return (VariableKind)(~(int)a);
}

struct Variable
{
public:
	VariableKind Type;
	byte Marker; // Actually a padding byte, but may later be helpful for the GC
private:
	// This may be unset if the value is smaller than the union below (currently 8 bytes)
	uint16_t _size;
public:
	// Important: Data must come last (because we sometimes take the address of this, and
	// the actual data may (for large value types) exceed the size of the union
	union
	{
		uint32_t Uint32;
		int32_t Int32;
		bool Boolean;
		void* Object;
		uint64_t Uint64;
		int64_t Int64;
		float Float;
		double Double;
	};

	Variable(const Variable& other)
		: Type(other.Type),
		Marker(other.Marker),
		_size(other._size),
		Uint64(other.Uint64)
	{
		if (other.fieldSize() > sizeof(Uint64))
		{
			throw stdSimple::ExecutionEngineException("Copy ctor not allowed on this instance");
		}
	}

	Variable& operator=(const Variable& other)
	{
		if (this == &other)
			return *this;
		Type = other.Type;
		Marker = other.Marker;
		Uint64 = other.Uint64;
		if (other._size > sizeof(Uint64) && this->Marker != VARIABLE_DECLARATION_MARKER && other.Marker != VARIABLE_DECLARATION_MARKER)
		{
			if (other.fieldSize() != _size)
			{
				throw stdSimple::ExecutionEngineException("Insufficient space to copy instance. Internal memory management error.");
			}
			// Copy the full size to the target.
			// WARN: This is dangerous, and we must make sure the target has actually allocated memory for this
			memcpy(&this->Uint32, &other.Uint32, other.fieldSize());
		}
		else
		{
			_size = other.fieldSize();
		}
		return *this;
	}

	Variable(uint32_t value, VariableKind type)
	{
		CommonInit();
		Uint32 = value;
		Type = type;
	}

	Variable(int32_t value, VariableKind type)
	{
		CommonInit();
		Int32 = value;
		Type = type;
	}

	Variable(VariableKind type)
	{
		CommonInit();
		Type = type;
	}

	Variable()
	{
		CommonInit();
	}

private: void CommonInit()
	{
		Marker = VARIABLE_DEFAULT_MARKER;
		_size = 0;
		Uint64 = 0;
		Type = VariableKind::Void;
	}
	
public:
	uint16_t fieldSize() const
	{
		if (Type == VariableKind::Object || Type == VariableKind::AddressOfVariable || Type == VariableKind::ReferenceArray || Type == VariableKind::ValueArray)
		{
			return sizeof(void*);
		}
		if (_size != 0)
		{
			return _size;
		}
		// 64 bit types have bit 4 set
		if (((int)Type & 16) != 0)
		{
			return 8;
		}
		return 4;
	}

	uint16_t memberSize() const
	{
		return _size;
	}

	void setSize(uint16_t size)
	{
		_size = size;
	}

	static size_t datasize()
	{
		return MAX(sizeof(void*), sizeof(uint64_t));
	}

	static size_t headersize()
	{
		return sizeof(Variable) - datasize(); // depending on alignment, this may be 4 or 8
	}
};

/// <summary>
/// Very similar to <see cref="Variable"/> however does not include the actual data
/// </summary>
struct VariableDescription
{
	VariableKind Type;
	byte Marker; // Actually a padding byte, but may later be helpful for the GC

	// This may be unset if the value is smaller than the union below (currently 8 bytes)
	uint16_t Size;

	VariableDescription(VariableKind type, size_t size)
	{
		CommonInit();
		Size = (uint16_t)size;
		Type = type;
	}

	VariableDescription()
	{
		CommonInit();
	}

private: void CommonInit()
{
	Marker = 0x37;
	Size = 0;
	Type = VariableKind::Void;
}

public:
	size_t fieldSize() const
	{
		if (Type == VariableKind::Object || Type == VariableKind::AddressOfVariable || Type == VariableKind::ReferenceArray || Type == VariableKind::ValueArray)
		{
			return sizeof(void*);
		}
		if (Size != 0)
		{
			return Size;
		}
		// 64 bit types have bit 4 set
		if (((int)Type & 16) != 0)
		{
			return 8;
		}
		return 4;
	}
};
