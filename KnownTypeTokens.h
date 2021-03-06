﻿
#pragma once

enum class KnownTypeTokens
{
    None = 0,
    Object = 1,
    Type = 2,
    ValueType = 3,
    String = 4,
    TypeInfo = 5,
    RuntimeType = 6,
    Nullable = 7,
    Enum = 8,
    Array = 9,
    ByReferenceByte = 10,
    Delegate = 11,
    MulticastDelegate = 12,
    Byte = 19,
    Int32 = 20,
    Uint32 = 21,
    Int64 = 22,
    Uint64 = 23,
    LargestKnownTypeToken = 40,
    IEnumerableOfT = 16777216,
    SpanOfT = 33554432,
};
