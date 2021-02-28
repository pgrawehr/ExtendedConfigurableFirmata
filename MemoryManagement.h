// MemoryManagement.h

#ifndef _MEMORYMANAGEMENT_h
#define _MEMORYMANAGEMENT_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

void* mallocEx(size_t length);
void* reallocEx(void* existingPtr, size_t newLength);

void freeInternal(void* ptr);

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
		ptr = nullptr;
	}
}



#endif

