#include <ConfigurableFirmata.h>
#include "MemoryManagement.h"
#include <malloc.h>

bool _executionMode = false;

// Enable to perform some allocation tracing
// This is intended to maybe improve some memory allocation methods (e.g. use a stack for allocating the execution stack)
#if 0 // defined SIM

#include <stack>

int totalSizeAllocated = 0;
int maxSizeUsed = 0;
std::stack<void*> _allocs;

void* mallocEx(size_t length)
{
	void* ret = malloc(length);
	if (ret != nullptr)
	{
		totalSizeAllocated += length;
		if (maxSizeUsed < totalSizeAllocated)
		{
			maxSizeUsed = totalSizeAllocated;
		}

		if (_executionMode)
		{
			_allocs.push(ret);
		}
	}
	return ret;
}

void CheckIsLastAndPop(void* ptr)
{
	if (_allocs.empty())
	{
		Firmata.sendStringf(F("Freeing memory when there's nothing allocated that needs freeing"));
		return;
	}

	void* expected = _allocs.top();
	_allocs.pop();
	if (ptr != expected)
	{
		Firmata.sendStringf(F("The last element allocated was 0x%x but we tried to free 0x%x"), expected, ptr);
		return;
	}
}

void* reallocEx(void* existingPtr, size_t newLength)
{
	int used = _msize(existingPtr);
	totalSizeAllocated -= used;
	totalSizeAllocated += newLength;

	void* ret = realloc(existingPtr, newLength);
	if (_executionMode)
	{
		CheckIsLastAndPop(existingPtr);
		_allocs.push(ret);
	}

	return ret;
}


void freeInternal(void* ptr)
{
	int used = _msize(ptr);
	totalSizeAllocated -= used;
	if (_executionMode)
	{
		CheckIsLastAndPop(ptr);
	}
	free(ptr);
}

void SetMemoryExecutionMode(bool executionModeEnabled)
{
	if (_executionMode == false && executionModeEnabled || executionModeEnabled == false)
	{
		// Reset if we start execution mode or end it.
		_allocs = std::stack<void*>();
	}
	_executionMode = executionModeEnabled;
}


#endif
