#pragma once


#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/// <summary>
/// Pointer arithmetic on byte level on other object types. This shall be used if the offset is in bytes, but T is some other pointer type.
/// </summary>
template <typename T>
T* AddBytes(T* inPtr, int offset)
{
	return (T*)(((byte*)inPtr) + offset);
}

template <typename T>
int ByteDifference(T* higher, T* lower)
{
	return (byte*)higher - (byte*)lower;
}

/// <summary>
/// Relocates a pointer (to compute the same address relative to a new base)
/// </summary>
/// <param name="inBasePtr">Input base pointer</param>
/// <param name="inPtr">Input pointer. Must be &gt; inBasePtr</param>
/// <param name="outBasePtr">New base pointer</param>
template <typename T>
T* Relocate(T* inBasePtr, T* inPtr, T* outBasePtr)
{
	int offset = ByteDifference(inPtr, inBasePtr);
	return AddBytes(outBasePtr, offset);
}

#ifndef _MSC_VER

// The ESP32 GCC variant doesn't support these, and changing the standard causes unclear side effects
typedef int32_t errno_t;

const size_t _TRUNCATE = -1;

errno_t strncpy_s(
	char* strDest,
	size_t numberOfElements,
	const char* strSource,
	size_t count
);

errno_t memcpy_s(void* dest, size_t destinationLength, void* source, size_t sourceLength);

#endif
