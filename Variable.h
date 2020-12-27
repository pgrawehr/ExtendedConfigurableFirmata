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
	Reference = 32, // Address of a variable
	RuntimeFieldHandle = 33, // So far this is a pointer to a constant initializer
	RuntimeTypeHandle = 34, // A type handle. The value is a type token
	AddressOfVariable = 35, // An address pointing to a variable slot on another method's stack or arglist
	StaticMember = 128, // type is defined by the first value it gets
};

struct Variable
{
	// Important: Data must come first (because we sometimes take the address of this)
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

	VariableKind Type;

	// This may be unset if the value is smaller than the union above (currently 8 bytes)
	uint16_t Size;

	Variable(uint32_t value, VariableKind type)
	{
		Int64 = 0;
		Uint32 = value;
		Type = type;
		Size = 0;
	}

	Variable(int32_t value, VariableKind type)
	{
		Int64 = 0;
		Int32 = value;
		Type = type;
		Size = 0;
	}

	Variable(VariableKind type)
	{
		Int64 = 0;
		Type = type;
		Object = nullptr;
		Size = 0;
	}

	Variable()
	{
		Uint64 = 0;
		Type = VariableKind::Void;
		Size = 0;
	}

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
