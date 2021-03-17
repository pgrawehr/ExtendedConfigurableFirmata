
#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "GarbageCollector.h"

byte* GarbageCollector::Allocate(size_t size)
{
	byte* ret = (byte*)mallocEx(size);
	if (ret == nullptr)
	{
		OutOfMemoryException::Throw("Out of memory allocating gc object");
		return nullptr;
	}

	memset(ret, 0, size);

	_gcAllocSize += size;

	_gcData.push_back(ret);
	return ret;
}

void GarbageCollector::PrintStatistics()
{
	Firmata.sendStringf(F("Total GC memory used: %d bytes in %d instances"), 8, _gcAllocSize, _gcData.size());
}

void GarbageCollector::Clear()
{
	PrintStatistics();
	for (size_t idx = 0; idx < _gcData.size(); idx++)
	{
		void* ptr = _gcData[idx];
		if (ptr != nullptr)
		{
			freeEx(ptr);
		}
		_gcData[idx] = nullptr;
	}

	_gcData.clear(true);
	_gcAllocSize = 0;
}

int GarbageCollector::Collect(int generation, FirmataIlExecutor* referenceContainer)
{
	return 0;
}
