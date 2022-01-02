// OverflowMath.h

#ifndef OVERFLOWMATH_h
#define OVERFLOWMATH_h

#include <ConfigurableFirmata.h>

// All of these functions return true in case of an overflow
#if _MSC_VER

#define ENABLE_INTSAFE_SIGNED_FUNCTIONS
#include <intsafe.h>
inline bool uadd_overflow(uint32_t x, uint32_t y, uint32_t* sum)
{
	return UInt32Add(x, y, sum) != S_OK;
}

inline bool sadd_overflow(int32_t x, int32_t y, int32_t* sum)
{
	return IntAdd(x, y, sum) != S_OK;
}

inline bool sadd_overflow(int64_t x, int64_t y, int64_t* sum)
{
	return Int64Add(x, y, sum) != S_OK;
}

inline bool uadd_overflow(uint64_t x, uint64_t y, uint64_t* sum)
{
	return UInt64Add(x, y, sum) != S_OK;
}

inline bool smul_overflow(int32_t x, int32_t y, int32_t* result)
{
	return Int32Mult(x, y, result) != S_OK;
}

inline bool umul_overflow(uint32_t x, uint32_t y, uint32_t* result)
{
	return UInt32Mult(x, y, result) != S_OK;
}

inline bool smul_overflow(int64_t x, int64_t y, int64_t* result)
{
	return Int64Mult(x, y, result) != S_OK;
}

inline bool umul_overflow(uint64_t x, uint64_t y, uint64_t* result)
{
	return UInt64Mult(x, y, result) != S_OK;
}

inline bool ssub_overflow(int32_t x, int32_t y, int32_t* result)
{
	return Int32Sub(x, y, result) != S_OK;
}

inline bool ssub_overflow(int64_t x, int64_t y, int64_t* result)
{
	return Int64Sub(x, y, result) != S_OK;
}

inline bool usub_overflow(uint32_t x, uint32_t y, uint32_t* result)
{
	return UInt32Sub(x, y, result) != S_OK;
}

inline bool usub_overflow(uint64_t x, uint64_t y, uint64_t* result)
{
	return UInt64Sub(x, y, result) != S_OK;
}

#elif defined ESP32

inline bool uadd_overflow(uint32_t x, uint32_t y, uint32_t* sum)
{
	return __builtin_uadd_overflow(x, y, sum);
};

inline bool uadd_overflow(uint64_t x, uint64_t y, uint64_t* sum)
{
	return __builtin_uaddll_overflow(x, y, sum);
};

inline bool usub_overflow(uint32_t x, uint32_t y, uint32_t* result)
{
	return __builtin_usub_overflow(x, y, result);
}

inline bool usub_overflow(uint64_t x, uint64_t y, uint64_t* result)
{
	return __builtin_usubll_overflow(x, y, result);
}

inline bool smul_overflow(int32_t x, int32_t y, int32_t* result)
{
	return __builtin_smul_overflow(x, y, result);
}

inline bool umul_overflow(uint32_t x, uint32_t y, uint32_t* result)
{
	return __builtin_umul_overflow(x, y, result);
}

inline bool smul_overflow(int64_t x, int64_t y, int64_t* result)
{
	return __builtin_smulll_overflow(x, y, result);
}

inline bool umul_overflow(uint64_t x, uint64_t y, uint64_t* result)
{
	return __builtin_umulll_overflow(x, y, result);
}

inline bool ssub_overflow(int32_t x, int32_t y, int32_t* result)
{
	return __builtin_ssub_overflow(x, y, result);
}

inline bool ssub_overflow(int64_t x, int64_t y, int64_t* result)
{
	return __builtin_ssubll_overflow(x, y, result);
}

inline bool sadd_overflow(int32_t x, int32_t y, int32_t* sum)
{
	return __builtin_sadd_overflow(x, y, sum);
};

inline bool sadd_overflow(int64_t x, int64_t y, int64_t* sum)
{
	return __builtin_saddll_overflow(x, y, sum);
}

#else // Neither MSVC nor ESP32 (can be SAM3X3) - Use manual implementation

// TODO: Many of these temporary implementations do NO overflow check or use a long, which is bad for performance

inline bool uadd_overflow(uint32_t x, uint32_t y, uint32_t* sum)
{
	long result = (long)x + (long)y;
	if (result < UINT32_MAX)
	{
		*sum = result;
		return false;
	}
	*sum = 0;
	return true;
};

inline bool uadd_overflow(uint64_t x, uint64_t y, uint64_t* sum)
{
	*sum = x + y;
	return false;
};

inline bool usub_overflow(uint32_t x, uint32_t y, uint32_t* result)
{
	if (y > x)
	{
		*result = 0;
		return false;
	}

	*result = x - y;
	return true;
}

inline bool usub_overflow(uint64_t x, uint64_t y, uint64_t* result)
{
	if (y > x)
	{
		*result = 0;
		return false;
	}

	*result = x - y;
	return true;
}

inline bool smul_overflow(int32_t x, int32_t y, int32_t* result)
{
	long result1 = (long)x * (long)y;
	if (result1 < INT32_MAX || result1 > INT32_MIN)
	{
		*result = result1;
		return false;
	}
	*result = 0;
	return true;
}

inline bool umul_overflow(uint32_t x, uint32_t y, uint32_t* result)
{
	long result1 = (long)x * (long)y;
	if (result1 < UINT32_MAX)
	{
		*result = result1;
		return false;
	}
	*result = 0;
	return true;
}

inline bool smul_overflow(int64_t x, int64_t y, int64_t* result)
{
	*result = x * y;
	return true;
}

inline bool umul_overflow(uint64_t x, uint64_t y, uint64_t* result)
{
	*result = x * y;
	return true;
}

inline bool ssub_overflow(int32_t x, int32_t y, int32_t* result)
{
	*result = x - y;
	return true;
}

inline bool ssub_overflow(int64_t x, int64_t y, int64_t* result)
{
	*result = x - y;
	return true;
}

inline bool sadd_overflow(int32_t x, int32_t y, int32_t* result)
{
	long result1 = (long)x + (long)y;
	if (result1 < INT32_MAX || result1 > INT32_MIN)
	{
		*result = result1;
		return false;
	}
	*result = 0;
	return true;
};

inline bool sadd_overflow(int64_t x, int64_t y, int64_t* sum)
{
	*sum = x + y;
	return true;
}

#endif

#endif
