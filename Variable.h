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
			if (other.fieldSize() > _size)
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

	bool isValueType()
	{
		return Type != VariableKind::Object && Type != VariableKind::ReferenceArray && Type != VariableKind::ValueArray && Type != VariableKind::AddressOfVariable;
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

	private:
	void CommonInit()
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


//==================================================================
// Semantics: if val can be represented as the exact same value
// when cast to Dst type, then FitsIn<Dst>(val) will return true;
// otherwise FitsIn returns false.
//
// Dst and Src must both be integral types.
//
// It's important to note that most of the conditionals in this
// function are based on static type information and as such will
// be optimized away. In particular, the case where the signs are
// identical will result in no code branches.

#ifdef _PREFAST_
#pragma warning(push)
#pragma warning(disable:6326) // PREfast warning: Potential comparison of a constant with another constant
#endif // _PREFAST_



template <typename Dst, bool dstIsSigned, typename Src, bool srcIsSigned>
inline bool FitsInInternal(Src val)
{
//#ifdef _MSC_VER
//	static_assert_no_msg(!__is_class(Dst));
//	static_assert_no_msg(!__is_class(Src));
//#endif

	if (srcIsSigned == dstIsSigned)
	{   // Src and Dst are equally signed
		if (sizeof(Src) <= sizeof(Dst))
		{   // No truncation is possible
			return true;
		}
		else
		{   // Truncation is possible, requiring runtime check
			return val == (Src)((Dst)val);
		}
	}
	else if (srcIsSigned)
	{   // Src is signed, Dst is unsigned
#ifdef __GNUC__
		// Workaround for GCC warning: "comparison is always
		// false due to limited range of data type."
		if (!(val == 0 || val > 0))
#else
		if (val < 0)
#endif
		{   // A negative number cannot be represented by an unsigned type
			return false;
		}
		else
		{
			if (sizeof(Src) <= sizeof(Dst))
			{   // No truncation is possible
				return true;
			}
			else
			{   // Truncation is possible, requiring runtime check
				return val == (Src)((Dst)val);
			}
		}
	}
	else
	{   // Src is unsigned, Dst is signed
		if (sizeof(Src) < sizeof(Dst))
		{   // No truncation is possible. Note that Src is strictly
			// smaller than Dst.
			return true;
		}
		else
		{   // Truncation is possible, requiring runtime check
#ifdef __GNUC__
			// Workaround for GCC warning: "comparison is always
			// true due to limited range of data type." If in fact
			// Dst were unsigned we'd never execute this code
			// anyway.
			return ((Dst)val > 0 || (Dst)val == 0) &&
#else
			return ((Dst)val >= 0) &&
#endif
				(val == (Src)((Dst)val));
		}
	}
}


// Requires that Dst is an integral type, and that DstMin and DstMax are the
// minimum and maximum values of that type, respectively.  Returns "true" iff
// "val" can be represented in the range [DstMin..DstMax] (allowing loss of precision, but
// not truncation).
template <int64_t DstMin, uint64_t DstMax>
inline bool FloatFitsInIntType(float val)
{
	float DstMinF = static_cast<float>(DstMin);
	float DstMaxF = static_cast<float>(DstMax);
	return DstMinF <= val && val <= DstMaxF;
}

template <int64_t DstMin, uint64_t DstMax>
inline bool DoubleFitsInIntType(double val)
{
	double DstMinD = static_cast<double>(DstMin);
	double DstMaxD = static_cast<double>(DstMax);
	return DstMinD <= val && val <= DstMaxD;
}

template <typename Dst, bool dstIsSigned, int64_t dstMin, int64_t dstMax>
inline bool FitsIn(Variable src)
{
	switch (src.Type)
	{
	case VariableKind::Uint64:
		return FitsInInternal<Dst, dstIsSigned, uint64_t, true>(src.Uint64);
	case VariableKind::Uint32:
	case VariableKind::AddressOfVariable:
	case VariableKind::Object:
	case VariableKind::FunctionPointer:
	case VariableKind::Reference:
	case VariableKind::ReferenceArray:
	case VariableKind::ValueArray:
	case VariableKind::Boolean:
		return FitsInInternal<Dst, dstIsSigned, uint32_t, true>(src.Uint32);
	case VariableKind::Int32:
		return FitsInInternal<Dst, dstIsSigned, int32_t, true>(src.Int32);
	case VariableKind::Int64:
		return FitsInInternal<Dst, dstIsSigned, int64_t, true>(src.Int64);
	case VariableKind::Float:
		return FloatFitsInIntType<dstMin, dstMax>(src.Float);
	case VariableKind::Double:
		return DoubleFitsInIntType<dstMin, dstMax>(src.Double);
	default:
		throw stdSimple::ExecutionEngineException("Unexpected case for FitsIn<>");
	}
	
}
