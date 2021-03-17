// GarbageCollector.h

#pragma once

#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "ObjectVector.h"

class FirmataIlExecutor;

/// <summary>
/// A continuous block of GC-Controlled memory. Within each block, a short value is used as next pointer.
/// If the next pointer is negative, it means the block is free.
/// </summary>
class GcBlock
{
public:
	byte* BlockStart;
	short BlockSize;
	short FreeBytesInBlock;
	byte* Tail;
};

class GarbageCollector
{
public:
	GarbageCollector()
	{
		_gcAllocSize = 0;
	}

	byte* TryAllocateFromBlock(GcBlock& block, size_t size);
	byte* Allocate(size_t size);

	int Collect(int generation, FirmataIlExecutor* referenceContainer);

	void Clear();

	void PrintStatistics();
private:
	int _gcAllocSize;
	stdSimple::vector<void*, size_t, 2000> _gcData;
	stdSimple::vector<GcBlock, size_t, 10> _gcBlocks;
};


