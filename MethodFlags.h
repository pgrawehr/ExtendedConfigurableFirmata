
#pragma once

enum class MethodFlags
{
    None = 0,
    Static = 1,
    Virtual = 2,
    SpecialMethod = 4,
    Void = 8,
    Ctor = 16,
    Abstract = 32,
};
