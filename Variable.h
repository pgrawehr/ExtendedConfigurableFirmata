#pragma once

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include "openum.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

enum class VariableKind : byte
{
	Void = 0, // The slot contains no data
	Uint32 = 1, // The slot contains unsigned integer data
	Int32 = 2, // The slot contains signed integer data
	Boolean = 3, // The slot contains true or false
	Object = 4, // The slot contains an object reference
	Method = 5,
	ValueArray = 6, // The slot contains a reference to an array of value types (inline)
	ReferenceArray = 7, // The slot contains a reference to an array of reference types
	Float = 8,
	LargeValueType = 9, // The slot contains a large value type
	Int64 = 16 + 1,
	Uint64 = 16 + 2,
	Double = 16 + 4,
	RuntimeFieldHandle = 33, // So far this is a pointer to a constant initializer
	RuntimeTypeHandle = 34, // A type handle. The value is a type token
	AddressOfVariable = 35, // An address pointing to a variable slot on another method's stack or arglist (obtained by LDLOCA or LDARGA)
	StaticMember = 128, // type is defined by the first value it gets
};

struct Variable
{
	VariableKind Type;
	byte Marker; // Actually a padding byte, but may later be helpful for the GC

	// This may be unset if the value is smaller than the union below (currently 8 bytes)
	uint16_t Size;
	
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
		Marker = 0x37;
		Size = 0;
		Uint64 = 0;
		Type = VariableKind::Void;
	}
	
public:
	size_t fieldSize() const
	{
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

	static size_t datasize()
	{
		return MAX(sizeof(void*), sizeof(uint64_t));
	}
};
