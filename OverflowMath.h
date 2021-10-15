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

#else

inline bool uadd_overflow(uint32_t x, uint32_t y, uint32_t* sum)
{
	return __builtin_uadd_overflow(x, y, sum);
}

bool uaddl_overflow(uint64_t x, uint64_t y, uint64_t* sum);
bool usub_overflow(uint32_t x, uint32_t y, uint32_t* diff);
bool usubl_overflow(unsigned long x, unsigned long y, unsigned long* diff);
bool usubll_overflow(unsigned long long x, unsigned long long y, unsigned long long* diff);
bool umul_overflow(unsigned x, unsigned y, unsigned* prod);
bool umull_overflow(unsigned long x, unsigned long y, unsigned long* prod);
bool umulll_overflow(unsigned long long x, unsigned long long y, unsigned long long* prod);
bool sadd_overflow(int x, int y, int* sum);
bool saddl_overflow(long x, long y, long* sum);
bool saddll_overflow(long long x, long long y, long long* sum);
bool ssub_overflow(int x, int y, int* diff);
bool ssubl_overflow(long x, long y, long* diff);
bool ssubll_overflow(long long x, long long y, long long* diff);
bool smul_overflow(int x, int y, int* prod);
bool smull_overflow(long x, long y, long* prod);
bool smulll_overflow(long long x, long long y, long long* prod);

#endif

#endif

