#include <ConfigurableFirmata.h>
#include "MemoryManagement.h"
#include <malloc.h>

#ifdef SIM
int totalSizeAllocated = 0;
int maxSizeUsed = 0;
#endif

void* mallocEx(size_t length)
{
	void* ret = malloc(length);
#ifdef SIM
	if (ret != nullptr)
	{
		totalSizeAllocated += length;
		if (maxSizeUsed < totalSizeAllocated)
		{
			maxSizeUsed = totalSizeAllocated;
		}
	}
#endif
	return ret;
}

void* reallocEx(void* existingPtr, size_t newLength)
{
#ifdef SIM
	int used = _msize(existingPtr);
	totalSizeAllocated -= used;
	totalSizeAllocated += newLength;
#endif
	return realloc(existingPtr, newLength);
}


void freeInternal(void* ptr)
{
#ifdef SIM
	int used = _msize(ptr);
	totalSizeAllocated -= used;
#endif
	free(ptr);
}
