#pragma once
#include <ConfigurableFirmata.h>
#include "Exceptions.h"

typedef unsigned char CLR_UINT8;
typedef unsigned short CLR_UINT16;
typedef unsigned int CLR_UINT32;
typedef unsigned __int64 CLR_UINT64;
typedef signed char CLR_INT8;
typedef signed short CLR_INT16;
typedef signed int CLR_INT32;
typedef signed __int64 CLR_INT64;

typedef CLR_UINT16 CLR_OFFSET;
typedef CLR_UINT32 CLR_OFFSET_LONG;
typedef CLR_UINT16 CLR_IDX;
typedef CLR_UINT16 CLR_STRING;
typedef CLR_UINT16 CLR_SIG;
typedef const CLR_UINT8 *CLR_PMETADATA;

//--//
// may need to change later
typedef CLR_INT64 CLR_INT64_TEMP_CAST;
typedef CLR_UINT64 CLR_UINT64_TEMP_CAST;

typedef uint32_t HRESULT;
typedef unsigned int FLASH_WORD;

#define ULONGLONGCONSTANT(x) (x)
#define NATIVE_PROFILE_CLR_CORE()

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

#define NANOCLR_NO_ASSEMBLY_STRINGS

#define NANOCLR_DEBUG_STOP() { throw stdSimple::ExecutionEngineException(); }

struct CLR_RT_HeapBlock;
struct CLR_RT_HeapBlock_Node;

struct CLR_RT_HeapBlock_WeakReference;
struct CLR_RT_HeapBlock_String;
struct CLR_RT_HeapBlock_Array;
struct CLR_RT_HeapBlock_Delegate;
struct CLR_RT_HeapBlock_Delegate_List;
struct CLR_RT_HeapBlock_BinaryBlob;

struct CLR_RT_HeapBlock_Button;
struct CLR_RT_HeapBlock_Lock;
struct CLR_RT_HeapBlock_LockRequest;
struct CLR_RT_HeapBlock_Timer;
struct CLR_RT_HeapBlock_WaitForObject;
struct CLR_RT_HeapBlock_Finalizer;
struct CLR_RT_HeapBlock_MemoryStream;

struct CLR_RT_HeapCluster;
struct CLR_RT_GarbageCollector;

struct CLR_RT_DblLinkedList;
struct CLR_RT_Assembly;
struct CLR_RT_TypeSystem;
struct CLR_RT_TypeDescriptor;

struct CLR_RT_Assembly_Instance;
struct CLR_RT_TypeSpec_Instance;
struct CLR_RT_TypeDef_Instance;
struct CLR_RT_MethodDef_Instance;
struct CLR_RT_FieldDef_Instance;

struct CLR_RT_StackFrame;
struct CLR_RT_SubThread;
struct CLR_RT_Thread;
struct CLR_RT_ExecutionEngine;

struct CLR_RT_ExecutionEngine_PerfCounters;

struct CLR_HW_Hardware;

enum CLR_DataType // KEEP IN SYNC WITH Microsoft.SPOT.DataType!!
{
	DATATYPE_VOID, // 0 bytes

	DATATYPE_BOOLEAN, // 1 byte
	DATATYPE_I1,      // 1 byte
	DATATYPE_U1,      // 1 byte

	DATATYPE_CHAR, // 2 bytes
	DATATYPE_I2,   // 2 bytes
	DATATYPE_U2,   // 2 bytes

	DATATYPE_I4, // 4 bytes
	DATATYPE_U4, // 4 bytes
	DATATYPE_R4, // 4 bytes

	DATATYPE_I8,       // 8 bytes
	DATATYPE_U8,       // 8 bytes
	DATATYPE_R8,       // 8 bytes
	DATATYPE_DATETIME, // 8 bytes     // Shortcut for System.DateTime
	DATATYPE_TIMESPAN, // 8 bytes     // Shortcut for System.TimeSpan
	DATATYPE_STRING,

	DATATYPE_LAST_NONPOINTER = DATATYPE_TIMESPAN,      // This is the last type that doesn't need to be relocated.
	DATATYPE_LAST_PRIMITIVE_TO_PRESERVE = DATATYPE_R8, // All the above types don't need fix-up on assignment.
#if defined(NANOCLR_NO_ASSEMBLY_STRINGS)
	DATATYPE_LAST_PRIMITIVE_TO_MARSHAL = DATATYPE_STRING, // All the above types can be marshaled by assignment.
#else
	DATATYPE_LAST_PRIMITIVE_TO_MARSHAL = DATATYPE_TIMESPAN, // All the above types can be marshaled by assignment.
#endif
	DATATYPE_LAST_PRIMITIVE = DATATYPE_STRING, // All the above types don't need fix-up on assignment.

	DATATYPE_OBJECT,    // Shortcut for System.Object
	DATATYPE_CLASS,     // CLASS <class Token>
	DATATYPE_VALUETYPE, // VALUETYPE <class Token>
	DATATYPE_SZARRAY,   // Shortcut for single dimension zero lower bound array SZARRAY <type>
	DATATYPE_BYREF,     // BYREF <type>

	////////////////////////////////////////

	DATATYPE_FREEBLOCK,
	DATATYPE_CACHEDBLOCK,
	DATATYPE_ASSEMBLY,
	DATATYPE_WEAKCLASS,
	DATATYPE_REFLECTION,
	DATATYPE_ARRAY_BYREF,
	DATATYPE_DELEGATE_HEAD,
	DATATYPE_DELEGATELIST_HEAD,
	DATATYPE_OBJECT_TO_EVENT,
	DATATYPE_BINARY_BLOB_HEAD,

	DATATYPE_THREAD,
	DATATYPE_SUBTHREAD,
	DATATYPE_STACK_FRAME,
	DATATYPE_TIMER_HEAD,
	DATATYPE_LOCK_HEAD,
	DATATYPE_LOCK_OWNER_HEAD,
	DATATYPE_LOCK_REQUEST_HEAD,
	DATATYPE_WAIT_FOR_OBJECT_HEAD,
	DATATYPE_FINALIZER_HEAD,
	DATATYPE_MEMORY_STREAM_HEAD, // SubDataType?
	DATATYPE_MEMORY_STREAM_DATA, // SubDataType?

	DATATYPE_SERIALIZER_HEAD,      // SubDataType?
	DATATYPE_SERIALIZER_DUPLICATE, // SubDataType?
	DATATYPE_SERIALIZER_STATE,     // SubDataType?

	DATATYPE_ENDPOINT_HEAD,

	// These constants are shared by Debugger.dll, and cannot be conditionally compiled away.
	// This adds a couple extra bytes to the lookup table.  But frankly, the lookup table should probably
	// be shrunk to begin with.  Most of the datatypes are used just to tag memory.
	// For those datatypes, perhaps we should use a subDataType instead (probably what the comments above are about).

	DATATYPE_RADIO_LAST = DATATYPE_ENDPOINT_HEAD + 3,

	DATATYPE_IO_PORT,
	DATATYPE_IO_PORT_LAST = DATATYPE_RADIO_LAST + 1,

	DATATYPE_VTU_PORT_LAST = DATATYPE_IO_PORT_LAST + 1,

#if defined(NANOCLR_APPDOMAINS)
	DATATYPE_APPDOMAIN_HEAD,
	DATATYPE_TRANSPARENT_PROXY,
	DATATYPE_APPDOMAIN_ASSEMBLY,
#endif
	DATATYPE_APPDOMAIN_LAST = DATATYPE_VTU_PORT_LAST + 3,

	DATATYPE_FIRST_INVALID,

	// Type modifies. This is exact copy of VALUES ELEMENT_TYPE_* from CorHdr.h
	//

	DATATYPE_TYPE_MODIFIER = 0x40,
	DATATYPE_TYPE_SENTINEL = 0x01 | DATATYPE_TYPE_MODIFIER, // sentinel for varargs
	DATATYPE_TYPE_PINNED = 0x05 | DATATYPE_TYPE_MODIFIER,
	DATATYPE_TYPE_R4_HFA = 0x06 | DATATYPE_TYPE_MODIFIER, // used only internally for R4 HFA types
	DATATYPE_TYPE_R8_HFA = 0x07 | DATATYPE_TYPE_MODIFIER, // used only internally for R8 HFA types
};

enum CLR_ReflectionType
{
	REFLECTION_INVALID = 0x00,
	REFLECTION_ASSEMBLY = 0x01,
	REFLECTION_TYPE = 0x02,
	REFLECTION_TYPE_DELAYED = 0x03,
	REFLECTION_CONSTRUCTOR = 0x04,
	REFLECTION_METHOD = 0x05,
	REFLECTION_FIELD = 0x06,
};

typedef void (*CLR_RT_MarkingHandler)(CLR_RT_HeapBlock_BinaryBlob* ptr);
typedef void (*CLR_RT_RelocationHandler)(CLR_RT_HeapBlock_BinaryBlob* ptr);
typedef void (*CLR_RT_HardwareHandler)();

struct CLR_RT_TypeDef_Index
{
	CLR_UINT32 m_data;

	//--//

	void Clear()
	{
		m_data = 0;
	}

	void Set(CLR_UINT32 idxAssm, CLR_UINT32 idxType)
	{
		m_data = idxAssm << 16 | idxType;
	}

	//--//

	CLR_IDX Assembly() const
	{
		return (CLR_IDX)(m_data >> 16);
	}
	CLR_IDX Type() const
	{
		return (CLR_IDX)(m_data);
	}
};



struct CLR_RT_MethodDef_Index
{
	CLR_UINT32 m_data;

	//--//

	void Clear()
	{
		m_data = 0;
	}

	void Set(CLR_UINT32 idxAssm, CLR_UINT32 idxMethod)
	{
		m_data = idxAssm << 16 | idxMethod;
	}

	//--//

	CLR_IDX Assembly() const
	{
		return (CLR_IDX)(m_data >> 16);
	}
	CLR_IDX Method() const
	{
		return (CLR_IDX)(m_data);
	}
};

struct CLR_RT_ReflectionDef_Index
{
	CLR_UINT16 m_kind; // CLR_ReflectionType
	CLR_UINT16 m_levels;

	union {
		CLR_RT_TypeDef_Index m_type;
		CLR_RT_MethodDef_Index m_method;
		CLR_UINT32 m_raw;
	} m_data;

	//--//

	void Clear();

	CLR_UINT32 GetTypeHash() const;

	void InitializeFromHash(CLR_UINT32 hash);

	CLR_UINT64 GetRawData() const;
	void SetRawData(CLR_UINT64 data);

	//--//

	static bool Convert(CLR_RT_HeapBlock& ref, CLR_RT_Assembly_Instance& inst);
	static bool Convert(CLR_RT_HeapBlock& ref, CLR_RT_TypeDef_Instance& inst, CLR_UINT32* levels);
	static bool Convert(CLR_RT_HeapBlock& ref, CLR_RT_MethodDef_Instance& inst);
	static bool Convert(CLR_RT_HeapBlock& ref, CLR_RT_FieldDef_Instance& inst);
	static bool Convert(CLR_RT_HeapBlock& ref, CLR_UINT32& hash);
};
