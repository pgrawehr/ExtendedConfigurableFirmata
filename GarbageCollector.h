// GarbageCollector.h

#pragma once

#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "ObjectVector.h"

class FirmataIlExecutor;

class GarbageCollector
{
public:
	GarbageCollector()
	{
		_gcAllocSize = 0;
	}
	
	byte* Allocate(size_t size);

	int Collect(int generation, FirmataIlExecutor* referenceContainer);

	void Clear();

	void PrintStatistics();
private:
	int _gcAllocSize;
	stdSimple::vector<void*, size_t, 2000> _gcData;
};


