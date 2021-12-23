// GarbageCollector.h

#pragma once

#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "ObjectVector.h"

class FirmataIlExecutor;

/* This must be smaller than 32k, because we use only 2 bytes for the next pointer */
const uint32_t DEFAULT_GC_BLOCK_SIZE = 8192;
const byte BLOCK_MARKER = 0xf7;

enum class BlockFlags : byte
{
	Used = 0,
	Free = 1,
};

inline BlockFlags operator | (BlockFlags lhs, BlockFlags rhs)
{
	return (BlockFlags)((byte)lhs | (byte)rhs);
}

inline BlockFlags operator & (BlockFlags lhs, BlockFlags rhs)
{
	return (BlockFlags)((byte)lhs & (byte)rhs);
}


// This represents the header for one memory block returned by Allocate()
struct BlockHd
{
	uint16_t BlockSize;
	byte Marker; // should be 0xf7
	BlockFlags flags;

	bool IsFree()
	{
		return ((flags & BlockFlags::Free) == BlockFlags::Free);
	}

	static BlockHd* Cast(void* address)
	{
		return (BlockHd*)address;
	}

	static void SetBlockAtAddress(void* address, uint16_t size, BlockFlags flags)
	{
		BlockHd* block_hd = Cast(address);
		block_hd->BlockSize = size;
		block_hd->Marker = BLOCK_MARKER;
		block_hd->flags = flags;
	}

	static short GetBlockSizeAtAddress(void* address)
	{
		BlockHd* block_hd = Cast(address);
		return block_hd->BlockSize;
	}
};

// This is the block allignment size and must also be equal to the size of the above struct
const uint32_t ALLOCATE_ALLIGNMENT = (sizeof(BlockHd));

/// <summary>
/// A continuous block of GC-Controlled memory. Within each block, a header of type BlockHd is used to separate the elements
/// </summary>
class GcBlock
{
public:
	BlockHd* BlockStart;
	uint16_t BlockSize;
	uint16_t FreeBytesInBlock;
	BlockHd* Tail;
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
		_numAllocsSinceLastGc = 0;
		_bytesAllocatedSinceLastGc = 0;
	}

	byte* TryAllocateFromBlock(GcBlock& block, uint32_t size);
	byte* Allocate(uint32_t size);
	void ValidateBlocks();

	void MarkDependentHandles(FirmataIlExecutor* referenceContainer);
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
	int _numAllocsSinceLastGc;
	int _bytesAllocatedSinceLastGc;
	stdSimple::vector<GcBlock, size_t, 10> _gcBlocks;
};
