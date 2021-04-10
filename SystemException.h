
#pragma once

enum class SystemException
{
    None = 0,
    StackOverflow = 1,
    NullReference = 2,
    MissingMethod = 3,
    InvalidOpCode = 4,
    DivideByZero = 5,
    IndexOutOfRange = 6,
    OutOfMemory = 7,
    ArrayTypeMismatch = 8,
    InvalidOperation = 9,
    ClassNotFound = 10,
    InvalidCast = 11,
    NotSupported = 12,
    FieldAccess = 13,
    Overflow = 14,
    Io = 15,
    CustomException = 20,
    ExecutionEngine = 21,
};
