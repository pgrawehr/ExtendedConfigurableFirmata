// MemoryManagement.h

#ifndef _MEMORYMANAGEMENT_h
#define _MEMORYMANAGEMENT_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#if 0
void* mallocEx(size_t length);
void* reallocEx(void* existingPtr, size_t newLength);

void freeInternal(void* ptr);

void SetMemoryExecutionMode(bool executionModeEnabled);
#endif

static void* _glastAlloc;
#define mallocEx(length) (_glastAlloc = malloc(length)); {if (!_glastAlloc) Firmata.sendStringf(F("Failed to allocate %d bytes"), length);}
#define reallocEx(ptr, newLength) realloc(ptr, newLength);
#define freeInternal(ptr) free(ptr);
#define SetMemoryExecutionMode(mode)

template<class T>
void freeEx(T*& ptr)
{
	if (ptr)
	{
		freeInternal(ptr);
		ptr = nullptr;
	}
}

template<class T>
void deleteEx(T*& ptr)
{
	if (ptr)
	{
		ptr->~T();
		freeEx(ptr);
	}
}



#endif
