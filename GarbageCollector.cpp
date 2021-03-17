
#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "GarbageCollector.h"
#include "SelfTest.h"

#define DEFAULT_GC_BLOCK_SIZE 8096

byte* GarbageCollector::Allocate(size_t size)
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
	
	_gcAllocSize += size;

	_gcData.push_back(ret);
	return ret;
}

byte* GarbageCollector::TryAllocateFromBlock(GcBlock& block, size_t size)
{
	size_t realSizeToReserve = size;
	byte* ret = nullptr;
	if (realSizeToReserve % 2)
	{
		realSizeToReserve += 1;
	}
	if (block.Tail + realSizeToReserve + 2 < block.BlockStart + block.BlockSize)
	{
		// There's room at the end of the block. Just use this.
		short* hd = (short*)block.Tail;
		short availableToEnd = -*hd;
		ASSERT(*hd < 0 && (availableToEnd >= realSizeToReserve));
		*hd = realSizeToReserve;
		ret = (byte*)AddBytes(hd, 2);
		hd = AddBytes(hd, 2 + realSizeToReserve);
		*hd = availableToEnd - 2 - realSizeToReserve;
		return ret;
	}
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
