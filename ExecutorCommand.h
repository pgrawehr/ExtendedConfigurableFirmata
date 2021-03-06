﻿
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
    ArrayOperations = 18,
    Nack = 126,
    Ack = 127,
};
