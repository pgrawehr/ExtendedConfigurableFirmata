
#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "GarbageCollector.h"
#include "SelfTest.h"

#define DEFAULT_GC_BLOCK_SIZE 8192

void GarbageCollector::Init(FirmataIlExecutor* referenceContainer)
{
	// this performs a GC self-test
	byte* first = Allocate(20);
	byte* second = Allocate(31);
	byte* third = Allocate(40);
	memset(first, 1, 20);
	memset(second, 2, 31);
	memset(third, 3, 40);

	ValidateBlocks();
	first = second = third = nullptr;
	int collected = Collect(0, referenceContainer);
	// ASSERT(collected > 90);

	ValidateBlocks();
}


byte* GarbageCollector::Allocate(int size)
{
	byte* ret = nullptr;
	for (size_t i = 0; i < _gcBlocks.size(); i++)
	{
		ret = TryAllocateFromBlock(_gcBlocks[i], size);
		if (ret != nullptr)
		{
			break;
		}
	}

	if (ret == nullptr)
	{
		// Allocate a new block
		int sizeToAllocate = MAX(DEFAULT_GC_BLOCK_SIZE, size + 4);
		void* newBlockPtr = nullptr;
		while(newBlockPtr == nullptr && sizeToAllocate >= size)
		{
			newBlockPtr = malloc(sizeToAllocate);
			if (newBlockPtr == nullptr)
			{
				// Another if, to make sure we keep the size of the block we actually allocated
				sizeToAllocate = sizeToAllocate / 2;
			}
		}

		if (newBlockPtr == nullptr)
		{
			// Still null? That's bad. Either the memory is completely exhausted, or the requested block is just way to large
			OutOfMemoryException::Throw("Out of memory increasing GC controlled memory");
		}
		
		GcBlock block;
		block.BlockSize = sizeToAllocate;
		block.BlockStart = (byte*)newBlockPtr;
		block.FreeBytesInBlock = sizeToAllocate - 2;
		block.Tail = block.BlockStart;
		*((short*)newBlockPtr) = -block.FreeBytesInBlock;
		
		_gcBlocks.push_back(block);

		ret = TryAllocateFromBlock(_gcBlocks.back(), size);
	}

	ValidateBlocks();
	_gcAllocSize += size;
	
	return ret;
}

void GarbageCollector::ValidateBlocks()
{
	for (size_t i = 0; i < _gcBlocks.size(); i++)
	{
		GcBlock& block = _gcBlocks[i];
		int blockLen = block.BlockSize;
		int offset = 0;
		short* hd = (short*)block.BlockStart;
		while (offset < blockLen)
		{
			short entry = *hd;
			if (entry == 0)
			{
				throw ExecutionEngineException("0-Block in memory list. That shouldn't happen.");
			}
			if (entry < 0)
			{
				entry = -entry;
			}
			if (entry > blockLen)
			{
				// A single entry cannot be larger than the memory block - something is wrong.
				throw ExecutionEngineException("Block list inconsistent");
			}
			
			hd = (short*)AddBytes(hd, entry + 2);
			offset = offset + entry + 2;
		}

		// At the end, the last block must end exactly at the block end.
		if (offset != blockLen)
		{
			throw ExecutionEngineException("Memory list inconsistent");
		}
	}
}

byte* GarbageCollector::TryAllocateFromBlock(GcBlock& block, int size)
{
	if (size == 0)
	{
		// Allocating empty blocks always returns the same address (that's probably never going to happen, since
		// every object at least has a vtable)
		return block.BlockStart;
	}

	if (size > block.FreeBytesInBlock)
	{
		return nullptr;
	}
	
	int realSizeToReserve = size;
	byte* ret = nullptr;
	short* hd = nullptr;
	if (realSizeToReserve % 2)
	{
		realSizeToReserve += 1;
	}
	// The +2 here is so that we don't create a zero-length block at the end
	if (block.Tail + realSizeToReserve + 2 < block.BlockStart + block.BlockSize)
	{
		// There's room at the end of the block. Just use this.
		hd = (short*)block.Tail;
		short availableToEnd = -*hd;
		ASSERT(*hd < 0 && (availableToEnd >= realSizeToReserve));
		*hd = realSizeToReserve;
		ret = (byte*)AddBytes(hd, 2);
		hd = AddBytes(hd, 2 + realSizeToReserve);
		*hd = -(availableToEnd - 2 - realSizeToReserve); // It's free memory, so write negative value
		block.Tail = (byte*)hd;
		block.FreeBytesInBlock -= realSizeToReserve + 2;
		return ret;
	}

	// If we get here, the block has been filled for the first time, therefore move it's tail to the end
	block.Tail = block.BlockStart + block.BlockSize;
	// There's not enough room at the end of the block. Check whether we find a place within the block
	hd = (short*)block.BlockStart;
	while ((byte*)hd < block.BlockStart + block.BlockSize && *hd != 0)
	{
		short entry = (*hd);
		if (entry >= 0)
		{
			hd = AddBytes(hd, (entry + 2));
			continue;
		}
		entry = -entry;
		if (entry >= realSizeToReserve && entry <= 2 * realSizeToReserve)
		{
			if (realSizeToReserve >= entry + 4)
			{
				// Split up the new block (but make sure we don't split away a 0-byte block)
				ret = (byte*)AddBytes(hd, 2);
				*hd = realSizeToReserve;
				hd = AddBytes(hd, 2 + realSizeToReserve);
				*hd = entry - 2 - realSizeToReserve; // That's how much is left to the next header (hopefully)

				block.FreeBytesInBlock -= realSizeToReserve + 2;
				return ret;
			}
			ret = (byte*)AddBytes(hd, 2);
			*hd = entry; // Reserve the whole block
			block.FreeBytesInBlock -= entry + 1;
			return ret;
		}
		hd = AddBytes(hd, (entry + 2));
	}

	return nullptr;
}


void GarbageCollector::PrintStatistics()
{
	Firmata.sendStringf(F("Total GC memory used: %d bytes in %d instances"), 8, _gcAllocSize, _gcData.size());
}

void GarbageCollector::Clear()
{
	PrintStatistics();

	for (size_t idx1 = 0; idx1 < _gcBlocks.size(); idx1++)
	{
		free(_gcBlocks[idx1].BlockStart);
	}

	_gcBlocks.clear();
	
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
