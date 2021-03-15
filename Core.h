#pragma once
#include <ConfigurableFirmata.h>
#include "Exceptions.h"

#ifdef _MSC_VER
#undef INPUT

#include <Windows.h>
#endif

#ifdef STATIC_ASSERT_SUPPORTED
#define CT_ASSERT_STRING(x) #x
#define CT_ASSERT_UNIQUE_NAME(e, name)                                                                                 \
    static_assert((e), CT_ASSERT_STRING(name) "@" __FILE__ CT_ASSERT_STRING(__LINE__));
#define CT_ASSERT(e) static_assert((e), __FILE__ CT_ASSERT_STRING(__LINE__));
#else
// CT_ASSERT (compile-time assert) macro is used to test condition at compiler time and generate
// compiler error if condition is bool.
// Example: CT_ASSERT( sizeof( unsigned int ) == 2 ) would cause compilation error.
//          CT_ASSERT( sizeof( unsigned int ) == 4 ) compiles without error.
// Since this declaration is just typedef - it does not create any CPU code.
//
// Reason for CT_ASSERT_UNIQUE_NAME
// The possible problem with the macro - it creates multiple identical typedefs.
// It is not a problem in global scope, but if macro is used inside of struct - it generates warnings.
// CT_ASSERT_UNIQUE_NAME is the same in essence, but it provides a way to customize the name of the type.
#define CT_ASSERT_UNIQUE_NAME(e, name) typedef char __CT_ASSERT__##name[(e) ? 1 : -1];
#define CT_ASSERT(e)                   CT_ASSERT_UNIQUE_NAME(e, nanoclr)
#endif

typedef unsigned int FLASH_WORD;

typedef struct BLOCKSTORAGEDEVICE
{
	void* m_BSD;
	void* m_context;
} BlockStorageDevice;

struct BLOCKSTORAGESTREAM
{
	unsigned int BaseAddress;
	unsigned int MemoryMappedAddress;
	unsigned int CurrentIndex;
	unsigned int Length;
	unsigned int BlockLength;
	unsigned int Usage;
	unsigned int Flags;
	unsigned int RegionIndex;
	unsigned int RangeIndex;
	unsigned int CurrentUsage;
	BlockStorageDevice* Device;
};

typedef struct BLOCKSTORAGESTREAM BlockStorageStream;

// The following can probably be removed later
struct HAL_COMPLETION
{
	
};

struct CLR_RT_DriverInterruptMethods
{
	
};

struct CLR_RT_MethodHandler
{
	
};

int32_t HAL_Time_CurrentSysTicks();
int64_t HAL_Time_SysTicksToTime(int32_t cumulative);

#define NATIVE_PROFILE_CLR_CORE()

struct CLR_HW_Hardware
{
	void PrepareForGC()
	{
	}
};

static CLR_HW_Hardware g_CLR_HW_Hardware;

#define MAXSTRLEN(x) (sizeof(x) / sizeof(x[0]))

#include "nanoCLR_Types.h"
#include "nanoCLR_Runtime.h"
#include "nanoCLR_Runtime__HeapBlock.h"

