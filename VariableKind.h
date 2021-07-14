
#pragma once

enum class VariableKind
{
    Void = 0,
    Uint32 = 1,
    Int32 = 2,
    Boolean = 3,
    Object = 4,
    Method = 5,
    ValueArray = 6,
    ReferenceArray = 7,
    Float = 8,
    LargeValueType = 9,
    Int64 = 17,
    Uint64 = 18,
    Double = 20,
    Reference = 32,
    RuntimeFieldHandle = 33,
    RuntimeTypeHandle = 34,
    AddressOfVariable = 35,
    FunctionPointer = 36,
    NativeHandle = 37,
    StaticMember = 128,
};
