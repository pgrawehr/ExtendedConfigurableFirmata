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
		_totalAllocSize = 0;
		_totalAllocations = 0;
		_currentMemoryUsage = 0;
		_maxMemoryUsage = 0;
	}

	byte* TryAllocateFromBlock(GcBlock& block, uint32_t size);
	byte* Allocate(uint32_t size);
	void ValidateBlocks();

	int Collect(int generation, FirmataIlExecutor* referenceContainer);

	void Clear();

	void PrintStatistics();

	void Init(FirmataIlExecutor* referenceContainer);
	int64_t TotalAllocatedBytes() const
	{
		return _totalAllocSize;
	}

	int64_t TotalMemory()
	{
		return _currentMemoryUsage;
	}

	int64_t AllocatedMemory();
private:
	void MarkAllFree();
	void MarkAllFree(GcBlock& block);
	int ComputeFreeBlockSizes();
	void MarkStatics(FirmataIlExecutor* referenceContainer);
	void MarkStack(FirmataIlExecutor* referenceContainer);
	bool IsValidMemoryPointer(void* ptr);
	void MarkVariable(Variable& variable, FirmataIlExecutor* referenceContainer);

	int _totalAllocSize;
	int _totalAllocations;
	int _currentMemoryUsage;
	int _maxMemoryUsage;
	stdSimple::vector<void*, size_t, 2000> _gcData; // TODO: Remove
	stdSimple::vector<GcBlock, size_t, 10> _gcBlocks;
};


