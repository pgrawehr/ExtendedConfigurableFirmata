﻿
#pragma once

enum class NativeMethod
{
    None = 0,
    HardwareLevelAccessSetPinMode = 1,
    HardwareLevelAccessWritePin = 2,
    HardwareLevelAccessReadPin = 3,
    HardwareLevelAccessGetPinMode = 4,
    HardwareLevelAccessIsPinModeSupported = 5,
    HardwareLevelAccessGetPinCount = 6,
    EnvironmentTickCount = 7,
    EnvironmentTickCount64 = 8,
    EnvironmentProcessorCount = 9,
    EnvironmentFailFast1 = 10,
    EnvironmentFailFast2 = 11,
    ArduinoNativeHelpersSleepMicroseconds = 12,
    ArduinoNativeHelpersGetMicroseconds = 13,
    ObjectEquals = 14,
    ObjectGetHashCode = 15,
    ObjectReferenceEquals = 16,
    ObjectToString = 17,
    ObjectGetType = 18,
    ObjectMemberwiseClone = 19,
    MonitorEnter = 20,
    MonitorWait = 21,
    MonitorExit = 22,
    StringEquals = 23,
    StringToString = 24,
    StringGetHashCode = 25,
    StringSetElem = 26,
    StringGetElem = 27,
    StringGetPinnableReference = 28,
    StringGetRawStringData = 29,
    StringEqualsStatic = 30,
    StringFastAllocateString = 31,
    StringUnEqualsStatic = 32,
    StringImplicitConversion = 33,
    StringEqualsStringComparison = 34,
    StringInternalAllocateString = 35,
    StringCtorSpan = 36,
    StringCompareTo = 37,
    StringCtorCharCount = 38,
    RuntimeHelpersInitializeArray = 39,
    RuntimeHelpersRunClassConstructor = 40,
    RuntimeHelpersIsReferenceOrContainsReferencesCore = 41,
    TypeGetTypeFromHandle = 42,
    TypeEquals = 43,
    TypeIsAssignableTo = 44,
    TypeIsEnum = 45,
    TypeTypeHandle = 46,
    TypeIsValueType = 47,
    TypeIsSubclassOf = 48,
    TypeIsAssignableFrom = 49,
    TypeCtor = 50,
    TypeMakeGenericType = 51,
    TypeGetHashCode = 52,
    TypeGetGenericTypeDefinition = 53,
    TypeGetGenericArguments = 54,
    TypeCreateInstanceForAnotherGenericParameter = 55,
    TypeIsArray = 56,
    TypeGetElementType = 57,
    TypeContainsGenericParameters = 58,
    TypeName = 59,
    TypeGetBaseType = 60,
    ValueTypeGetHashCode = 61,
    ValueTypeEquals = 62,
    ValueTypeToString = 63,
    BitConverterSingleToInt32Bits = 64,
    BitConverterDoubleToInt64Bits = 65,
    BitConverterHalfToInt16Bits = 66,
    BitConverterInt64BitsToDouble = 67,
    BitConverterInt32BitsToSingle = 68,
    BitConverterInt16BitsToHalf = 69,
    BitOperationsLog2SoftwareFallback = 70,
    BitOperationsTrailingZeroCount = 71,
    EnumGetHashCode = 72,
    EnumToUInt64 = 73,
    EnumInternalBoxEnum = 74,
    EnumInternalGetValues = 75,
    UnsafeNullRef = 76,
    UnsafeAs2 = 77,
    UnsafeAddByteOffset = 78,
    UnsafeSizeOfType = 79,
    UnsafeAsPointer = 80,
    UnsafeByteOffset = 81,
    UnsafeAreSame = 82,
    UnsafeSkipInit = 83,
    UnsafeIsAddressGreaterThan = 84,
    UnsafeIsAddressLessThan = 85,
    BufferMemmove = 86,
    BufferZeroMemory = 87,
    RuntimeHelpersGetHashCode = 88,
    RuntimeHelpersIsBitwiseEquatable = 89,
    RuntimeHelpersGetMethodTable = 90,
    RuntimeHelpersGetRawArrayData = 91,
    RuntimeHelpersGetMultiDimensionalArrayBounds = 92,
    RuntimeHelpersGetMultiDimensionalArrayRank = 93,
    RuntimeTypeHandleValue = 94,
    RuntimeTypeHandleGetCorElementType = 95,
    RuntimeHelpersEnumEquals = 96,
    Interop_GlobalizationGetCalendarInfo = 97,
    InteropGetRandomBytes = 98,
    ArduinoNativeI2cDeviceReadByte = 99,
    ArduinoNativeI2cDeviceReadSpan = 100,
    ArduinoNativeI2cDeviceWriteByte = 101,
    ArduinoNativeI2cDeviceWriteSpan = 102,
    ArduinoNativeI2cDeviceWriteRead = 103,
    ArduinoNativeI2cDeviceInit = 104,
    ByReferenceCtor = 105,
    ByReferenceValue = 106,
    InteropQueryPerformanceFrequency = 107,
    InteropQueryPerformanceCounter = 108,
    InterlockedCompareExchange_Object = 109,
    InterlockedExchangeAdd = 110,
    DelegateInternalEqualTypes = 111,
    DateTimeUtcNow = 112,
    MemoryMarshalGetArrayDataReference = 113,
    ArrayCopyCore = 114,
    ArrayClear = 115,
    ArrayInternalCreate = 116,
    ArraySetValue1 = 117,
    ArrayGetValue1 = 118,
    ArrayGetLength = 119,
    ActivatorCreateInstance = 120,
    GcCollect = 121,
    GcGetTotalMemory = 122,
    GcGetTotalAllocatedBytes = 123,
    GcTotalAvailableMemoryBytes = 124,
    MathCeiling = 125,
    MathFloor = 126,
    MathPow = 127,
    MathLog = 128,
    MathLog2 = 129,
    MathLog10 = 130,
    MathSin = 131,
    MathCos = 132,
    MathTan = 133,
    MathSqrt = 134,
    MathExp = 135,
    MathAbs = 136,
    DebugWriteLine = 137,
    ThreadGetCurrentThreadNative = 138,
    Interop_Kernel32CreateFile = 139,
    Interop_Kernel32SetLastError = 140,
    Interop_Kernel32GetLastError = 141,
    Interop_Kernel32SetFilePointerEx = 142,
    Interop_Kernel32CloseHandle = 143,
    Interop_Kernel32SetEndOfFile = 144,
    Interop_Kernel32WriteFile = 145,
    Interop_Kernel32WriteFileOverlapped = 146,
    Interop_Kernel32CancelIoEx = 147,
    Interop_Kernel32ReadFile = 148,
    Interop_Kernel32ReadFileOverlapped = 149,
    Interop_Kernel32FlushFileBuffers = 150,
    Interop_Kernel32GetFileInformationByHandleEx = 151,
    Interop_Kernel32QueryUnbiasedInterruptTime = 152,
};
