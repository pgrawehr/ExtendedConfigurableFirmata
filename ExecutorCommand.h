
#pragma once

enum class ExecutorCommand
{
    None = 0,
    DeclareMethod = 1,
    LoadIl = 3,
    StartTask = 4,
    ResetExecutor = 5,
    KillTask = 6,
    MethodSignature = 7,
    ClassDeclaration = 8,
    ClassDeclarationEnd = 9,
    ConstantData = 10,
    Interfaces = 11,
    CopyToFlash = 12,
    WriteFlashHeader = 13,
    CheckFlashVersion = 14,
    EraseFlash = 15,
    SetConstantMemorySize = 16,
    SpecialTokenList = 17,
    ExceptionClauses = 18,
    ArrayOperations = 19,
    QueryHardware = 20,
    ConditionalBreakpointHit = 30,
    BreakpointHit = 31,
    AddConditionalBreakpoint = 32,
    DeleteConditionalBreakpoint = 33,
    DeleteAllBreakpoints = 34,
    Reply = 125,
    Nack = 126,
    Ack = 127,
};
