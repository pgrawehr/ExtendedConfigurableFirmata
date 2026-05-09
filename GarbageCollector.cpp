
#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "GarbageCollector.h"
#include "SelfTest.h"
#include "FreeMemory.h"

void GarbageCollector::Init(FirmataIlExecutor* referenceContainer)
{
	ValidateBlocks();
	Clear(false, false);
}

byte* GarbageCollector::Allocate(uint32_t size, FirmataIlExecutor* referenceContainer)
{
	byte* ret = nullptr;
	TRACE(Firmata.sendStringf(F("Allocating %d bytes"), size));
	
	GcBlock* currentBlock = _gcBlockHead;
	while (currentBlock != nullptr)
	{
		ret = TryAllocateFromBlock(currentBlock, size);
		if (ret != nullptr)
		{
			// The last of our current blocks is getting full. Increase GC efforts
			if (currentBlock->Next == nullptr && currentBlock->FreeBytesInBlock < 512)
			{
				_gcPressureHigh = true;
			}
			break;
		}

		currentBlock = currentBlock->Next;
	}

	if (ret == nullptr && referenceContainer != nullptr)
	{
		// Very expensive, but is probably a good idea at this point, before we try to add a new block
		Collect(0, referenceContainer);
		ComputeFreeBlockSizes();
		currentBlock = _gcBlockHead;
		while (currentBlock != nullptr)
		{
			ret = TryAllocateFromBlock(currentBlock, size);
			if (ret != nullptr)
			{
				// The last of our current blocks is getting full. Increase GC efforts
				if (currentBlock->Next == nullptr && currentBlock->FreeBytesInBlock < 512)
				{
					_gcPressureHigh = true;
				}
				break;
			}

			currentBlock = currentBlock->Next;
		}
	}

	if (ret == nullptr)
	{
		Firmata.sendStringf(F("Unable to allocate a managed block of size %d."), size);
		// Allocate a new block
		_gcPressureHigh = true;
		uint32_t sizeToAllocate = MAX(DEFAULT_GC_BLOCK_SIZE, size + ALLOCATE_ALLIGNMENT);
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
			Firmata.sendStringf(F("Failed to increase size of GC controlled memory. Need a block of size %d, but couldn't get %d bytes of extra Heap"), size, sizeToAllocate);
			printMemoryStatistics();
			// Still null? That's bad. Either the memory is completely exhausted, or the requested block is just way to large
			OutOfMemoryException::Throw("Out of memory increasing GC controlled memory");
		}

		Firmata.sendStringf(F("Allocated new GC memory block at 0x%lx, size %ld"), newBlockPtr, sizeToAllocate);
		
		GcBlock* block = (GcBlock*)mallocEx(sizeof(GcBlock));
		if (block == nullptr)
		{
			free(newBlockPtr);
			OutOfMemoryException::Throw("Out of memory for memory management");
		}
		block->BlockSize = sizeToAllocate;
		block->BlockStart = (BlockHd*)newBlockPtr;
		block->FreeBytesInBlock = (uint16_t)(sizeToAllocate - ALLOCATE_ALLIGNMENT);
		block->Tail = block->BlockStart;
		block->Next = nullptr;
		BlockHd::SetBlockAtAddress(newBlockPtr, block->FreeBytesInBlock, BlockFlags::Free);

		_totalGcMemorySize += sizeToAllocate;
		if (_gcBlockHead == nullptr)
		{
			_gcBlockHead = block;
		}
		else
		{
			GcBlock* current = _gcBlockHead;
			while (current->Next != nullptr)
			{
				current = current->Next;
			}
			current->Next = block;
		}
		ret = TryAllocateFromBlock(block, size);

		PrintStatistics();
	}

	if (ret == nullptr)
	{
		Firmata.sendStringf(F("Out of GC memory when attempting to allocate %d bytes"), size);
		OutOfMemoryException::Throw("Out of GC memory.");
	}
#if GC_DEBUG_LEVEL >= 2
	ValidateBlocks();
#endif
	_totalAllocSize += size;
	_totalAllocations++;
	_bytesAllocatedSinceLastGc += size;
	_numAllocsSinceLastGc++;

	ASSERT(((uint32_t)ret % ALLOCATE_ALLIGNMENT) == 0, "GC_ALIGN");

	BlockHd* hd = BlockHd::Cast(ret - (int32_t)ALLOCATE_ALLIGNMENT);
	ASSERT(!hd->IsFree(), "GC_NOTFREE");
	ASSERT(hd->Marker == BLOCK_MARKER, "GC_NOTBLOCK");
	
	return ret;
}

void GarbageCollector::ValidateBlock(GcBlock* block)
{
	int blockLen = block->BlockSize;
	int offset = 0;
	BlockHd* hd = block->BlockStart;
	int free = 0;
	int used = 0;
	while (offset < blockLen)
	{
		int size = hd->BlockSize;
		/*if (size <= 0)
		{
			throw ExecutionEngineException("Invalid block size in memory list. That shouldn't happen.");
		}*/
			
		if (size > blockLen)
		{
			// A single entry cannot be larger than the memory block - something is wrong.
			throw ExecutionEngineException("Block list inconsistent");
		}

		if (hd->Marker != BLOCK_MARKER)
		{
			throw ExecutionEngineException("Block marker missing.");
		}

		if (hd->IsFree())
		{
			free += hd->BlockSize;
		}
		else
		{
			used += hd->BlockSize;
		}
		used += ALLOCATE_ALLIGNMENT; // For the block header

		hd = AddBytes(hd, size + ALLOCATE_ALLIGNMENT);
		offset = offset + size + ALLOCATE_ALLIGNMENT;
	}

	// At the end, the last block must end exactly at the block end.
	if (offset != blockLen)
	{
		throw ExecutionEngineException("Memory list inconsistent");
	}

	if (free != block->FreeBytesInBlock)
	{
		Firmata.sendStringf(F("Inconsistent free memory size. Expected %d bytes free, but actually %d bytes available"), block->FreeBytesInBlock, free);
	}

	if (free + used != block->BlockSize)
	{
		Firmata.sendStringf(F("Inconsistent memory allocation sizes. Free bytes %d, used bytes (including headers) %d. Sum is %d but should be %d"), free, used, free + used, block->BlockSize);
	}
}

void GarbageCollector::ValidateBlocks()
{
	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		ValidateBlock(current);
		current = current->Next;
	}
}

byte* GarbageCollector::AllocateBlock(GcBlock* block, uint32_t realSizeToReserve, BlockHd* hd)
{
	byte* ret;
	uint16_t thisBlockSize = hd->BlockSize;
	ASSERT(hd->BlockSize >= realSizeToReserve, "Attempted to reserve a block that's to small");
	if (realSizeToReserve >= thisBlockSize + ALLOCATE_ALLIGNMENT)
	{
		// Split up the new block (but make sure we don't split away a 0-byte block)
		ret = (byte*)AddBytes(hd, ALLOCATE_ALLIGNMENT);
		hd->BlockSize = realSizeToReserve;
		hd->flags = BlockFlags::Used;
		hd = AddBytes(hd, ALLOCATE_ALLIGNMENT + realSizeToReserve);
		if (hd < block->BlockStart + block->BlockSize)
		{
			BlockHd::SetBlockAtAddress(hd, thisBlockSize - ALLOCATE_ALLIGNMENT - realSizeToReserve, BlockFlags::Free);
		}
				
		block->FreeBytesInBlock -= realSizeToReserve + ALLOCATE_ALLIGNMENT;
	}
	else
	{
		// Take the whole block (it's exactly the size we need)
		ret = (byte*)AddBytes(hd, ALLOCATE_ALLIGNMENT);
		hd->flags = BlockFlags::Used; // Reserve the whole block
		block->FreeBytesInBlock -= thisBlockSize;
	}

	return ret;
}

byte* GarbageCollector::TryAllocateFromBlock(GcBlock* block, uint32_t size)
{
	ASSERT(size != 0, "GC_ZERO_ALLOC");

	if (size > block->FreeBytesInBlock)
	{
#if GC_DEBUG_LEVEL >= 2
		ValidateBlocks();
#endif
		return nullptr;
	}
	
	uint32_t realSizeToReserve = size;
	byte* ret = nullptr;
	BlockHd* hd = nullptr;
	if ((realSizeToReserve % ALLOCATE_ALLIGNMENT) != 0)
	{
		realSizeToReserve = (realSizeToReserve + ALLOCATE_ALLIGNMENT) & ~(ALLOCATE_ALLIGNMENT - 1);
	}
	// The + ALLOCATE_ALLIGNMENT here is so that we don't create a zero-length block at the end
	if (AddBytes(block->Tail, realSizeToReserve + ALLOCATE_ALLIGNMENT) < AddBytes(block->BlockStart, block->BlockSize))
	{
		// There's room at the end of the block. Just use this.
		hd = block->Tail;
		uint16_t availableToEnd = hd->BlockSize;
		ASSERT(hd->flags == BlockFlags::Free && (availableToEnd >= realSizeToReserve), "GC_BLOCK_ISSUE");
		hd->BlockSize = (uint16_t)realSizeToReserve;
		hd->flags = BlockFlags::Used;
		ret = (byte*)AddBytes(hd, ALLOCATE_ALLIGNMENT);
		hd = AddBytes(hd, ALLOCATE_ALLIGNMENT + realSizeToReserve);
		BlockHd::SetBlockAtAddress(hd, availableToEnd - (int)realSizeToReserve - ALLOCATE_ALLIGNMENT, BlockFlags::Free); // It's free memory
		block->Tail = hd;
		block->FreeBytesInBlock -= realSizeToReserve + ALLOCATE_ALLIGNMENT;
		return ret;
	}

	// If we get here, the block has been filled for the first time, therefore move it's tail to the end
	block->Tail = block->BlockStart + block->BlockSize;
	// There's not enough room at the end of the block. Check whether we find a place within the block
	hd = block->BlockStart;
	BlockHd* possibleHd = nullptr;
	while (hd < AddBytes(block->BlockStart, block->BlockSize) && hd->BlockSize != 0)
	{
		uint16_t thisBlockSize = hd->BlockSize;
		if (!hd->IsFree())
		{
			hd = AddBytes(hd, (thisBlockSize + ALLOCATE_ALLIGNMENT));
			continue;
		}

		if (thisBlockSize >= realSizeToReserve && thisBlockSize <= 2 * realSizeToReserve)
		{
			// We found an almost optimal block
			return AllocateBlock(block, realSizeToReserve, hd);
		}
		else if (thisBlockSize >= realSizeToReserve)
		{
			// We found a free block that would fit.
			// Remember it in case we find nothing better
			if (possibleHd == nullptr || possibleHd->BlockSize > thisBlockSize)
			{
				possibleHd = hd;
			}
		}

		hd = AddBytes(hd, (thisBlockSize + ALLOCATE_ALLIGNMENT));
	}

	if (possibleHd != nullptr)
	{
		return AllocateBlock(block, realSizeToReserve, possibleHd);
	}

	return nullptr;
}

void GarbageCollector::PrintStatistics()
{
	Firmata.sendStringf(F("Total GC memory allocated: %d bytes in %d instances"), _totalAllocSize, _totalAllocations);
	Firmata.sendStringf(F("Current/Maximum GC memory used: %d/%d bytes"), _currentMemoryUsage, _maxMemoryUsage);
	Firmata.sendStringf(F("Total size of GC controlled heap: %d"), _totalGcMemorySize);
	printMemoryStatistics();
}

void GarbageCollector::Clear(bool printStatistics, bool all)
{
	if (printStatistics)
	{
		PrintStatistics();
	}

	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		GcBlock* next = current->Next;
		freeEx(current->BlockStart);
		freeEx(current);
		current = next;
	}
	
	_gcBlockHead = nullptr;
	_totalGcMemorySize = 0;
	_totalAllocSize = 0;
	_totalAllocations = 0;
	_currentMemoryUsage = 0;
	_maxMemoryUsage = 0;
	_bytesAllocatedSinceLastGc = 0;
	_numAllocsSinceLastGc = 0;
}

/// <summary>
/// Return the total size of the memory in the GC blocks.
/// </summary>
int64_t GarbageCollector::AllocatedMemory()
{
	int64_t memorySum = 0;
	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		memorySum += current->BlockSize;
		current = current->Next;
	}

	return memorySum;
}

void GarbageCollector::MarkAllFree()
{
	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		MarkAllFree(current);
		current = current->Next;
	}
}

void GarbageCollector::MarkAllFree(GcBlock* block)
{
	int blockLen = block->BlockSize;
	int offset = 0;
	BlockHd* hd = block->BlockStart;
	while (offset < blockLen)
	{
		int blockSize = hd->BlockSize;

		hd->flags = BlockFlags::Free;

		hd = AddBytes(hd, blockSize + ALLOCATE_ALLIGNMENT);
		offset = offset + blockSize + ALLOCATE_ALLIGNMENT;
	}
}

/// <summary>
/// Computes the "result" of the garbage collect operation (how much memory was freed, how much total memory is now available etc.)
/// Also connects consecutive free blocks, so that the next allocation can use them as a single block.
/// </summary>
/// <returns>The number of bytes freed</returns>
int GarbageCollector::ComputeFreeBlockSizes()
{
	int totalFreed = 0;
	int totalMemoryInUse = 0;
	int largestFreeBlock = 0;
	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		uint32_t blockLen = current->BlockSize;
		uint32_t offset = 0;
		BlockHd* hd = current->BlockStart;
		// Chain adjacent free blocks
		while (offset < blockLen)
		{
			uint32_t entryLength = hd->BlockSize;
			uint32_t nextOffset = offset + entryLength + ALLOCATE_ALLIGNMENT;

			if (hd->IsFree() && nextOffset < blockLen)
			{
				BlockHd* hdNext = AddBytes(hd, entryLength + ALLOCATE_ALLIGNMENT);
				if (hdNext->IsFree())
				{
					// This and the next are free. Extend the size of this block to include the next (the ALLOCATE_ALIGNMENT
					// is the size of the next block header that we also free by this)
					hd->BlockSize = hd->BlockSize + hdNext->BlockSize + ALLOCATE_ALLIGNMENT;
					nextOffset = offset + hd->BlockSize + ALLOCATE_ALLIGNMENT;
					if (nextOffset >= blockLen)
					{
						// If we extended the tail block, we need to reset the tail pointer, otherwise
						// we would allocate a block from there, which is not valid
						current->Tail = hd;
						break;
					}

					// And continue on this block, because there could be another free block following
					continue;
				}
			}

			if (hd->IsFree() && hd->BlockSize > largestFreeBlock)
			{
				largestFreeBlock = hd->BlockSize;
			}

			hd = AddBytes(hd, entryLength + ALLOCATE_ALLIGNMENT);
			offset = nextOffset;
		}

		hd = current->BlockStart;
		int blockFree = 0;
		offset = 0;
		// Calculate free bytes in block
		while (offset < blockLen)
		{
			int entryLength = hd->BlockSize;

			if (hd->IsFree())
			{
				blockFree += entryLength;
#if GC_DEBUG_LEVEL >= 2
				{
					memset(AddBytes(hd, sizeof(BlockHd)), 0xaa, entryLength);
				}
#endif
			}
			else
			{
				totalMemoryInUse += entryLength;
			}

			hd = AddBytes(hd, entryLength + ALLOCATE_ALLIGNMENT);
			offset = offset + entryLength + ALLOCATE_ALLIGNMENT;
		}
		if (blockFree > current->FreeBytesInBlock)
		{
			totalFreed += blockFree - current->FreeBytesInBlock;
		}

		current->FreeBytesInBlock = (uint16_t)blockFree;
		current = current->Next;
	}

	_currentMemoryUsage = totalMemoryInUse;
	if (totalMemoryInUse > _maxMemoryUsage)
	{
		_maxMemoryUsage = totalMemoryInUse;
	}
	return totalFreed;
}

int GarbageCollector::Collect(int generation, FirmataIlExecutor* referenceContainer)
{
	if (generation >= 2 && !_gcPressureHigh)
	{
		// If the generation is given as 2, we skip the GC run if we think not much memory has been allocated
		if (_numAllocsSinceLastGc < 100 && _bytesAllocatedSinceLastGc < 5000)
		{
			return 0;
		}
	}
	TRACE(Firmata.sendString(F("Beginning GC")));
	MarkAllFree();
	MarkStatics(referenceContainer);
	MarkStacks(referenceContainer);

	MarkDependentHandles(referenceContainer);
	int result = ComputeFreeBlockSizes();
	TRACE(Firmata.sendString(F("GC done")));
	_numAllocsSinceLastGc = 0;
	_bytesAllocatedSinceLastGc = 0;
	_gcPressureHigh = false;
	return result;
}

void GarbageCollector::MarkDependentHandles(FirmataIlExecutor* referenceContainer)
{
	for (size_t i = 0; i < referenceContainer->_weakDependencies.size(); i++)
	{
		auto p = referenceContainer->_weakDependencies[i];
		BlockHd* hd = BlockHd::Cast((byte*)p.first - (int32_t)ALLOCATE_ALLIGNMENT);
		if (!hd->IsFree() && IsValidMemoryObject(p.second))
		{
			// If I got the concept of DependentHandle right, we shall mark the second as used when the first is.
			hd = BlockHd::Cast((byte*)p.second - (int32_t)ALLOCATE_ALLIGNMENT);
			hd->flags = BlockFlags::Used;
		}
	}
}


void GarbageCollector::MarkStatics(FirmataIlExecutor* referenceContainer)
{
	size_t offset = 0;
	byte* start = referenceContainer->_staticVector;

	if (start == nullptr)
	{
		return; // no static variables yet.
	}

	while (offset < referenceContainer->_staticVectorMemorySize)
	{
		// int currentToken = *AddBytes((int*)start, offset);
		Variable* ptr = (Variable*)AddBytes(start, offset + sizeof(int32_t));
		
		MarkVariable(*ptr, referenceContainer);

		offset += sizeof(int32_t) + 4 + ptr->fieldSize();
	}
}

void GarbageCollector::MarkStacks(FirmataIlExecutor* referenceContainer)
{
	for (int i = 0; i < MAX_THREADS; i++)
	{
		ThreadState* thread = referenceContainer->_threads[i];
		if (thread == nullptr)
		{
			continue;
		}

		ExecutionState* state = thread->rootOfExecutionStack;

		if (state == nullptr)
		{
			continue;
		}

		// The thread itself is a root object
		MarkVariable(thread->managedThreadInstance, referenceContainer);

		VariableListEntry* e = thread->threadStatics.first();
		while (e != nullptr)
		{
			Variable& ref = e->Data;
			MarkVariable(ref, referenceContainer);
			e = thread->threadStatics.next(e);
		}

		while (state != nullptr)
		{
			uint16_t pc;
			VariableDynamicStack* stack;
			VariableVector* locals;
			VariableVector* arguments;
			state->ActivateState(&pc, &stack, &locals, &arguments);
			VariableDynamicStack::Iterator stackIterator = stack->GetIterator();
			Variable* var;
			while ((var = stackIterator.next()) != nullptr)
			{
				MarkVariable(*var, referenceContainer);
			}

			for (int i = 0; i < locals->size(); i++)
			{
				Variable& v = locals->at(i);
				MarkVariable(v, referenceContainer);
			}

			for (int i = 0; i < arguments->size(); i++)
			{
				Variable& v = arguments->at(i);
				MarkVariable(v, referenceContainer);
			}

			VariableListEntry* e = state->_localStorage.first();
			while (e != nullptr)
			{
				Variable& ref = e->Data;
				MarkVariable(ref, referenceContainer);
				e = state->_localStorage.next(e);
			}

			ExceptionFrame* ex = state->_exceptionFrame;
			while (ex != nullptr)
			{
				MarkVariable(ex->Exception, referenceContainer);
				ex = ex->Next;
			}

			state = state->_next;
		}
	}
}

/// <summary>
/// Tests whether the given pointer could be an object pointer (means that it contains a value that could be an address in our heap)
/// </summary>

bool GarbageCollector::IsValidMemoryObject(void* ptr)
{
	BlockHd* containedInBlock;
	bool isObject;
	return IsValidMemoryObjectOrPointer(ptr, containedInBlock, isObject) && isObject;
}


bool GarbageCollector::IsValidMemoryObjectOrPointer(void* ptr, BlockHd *&containedInBlock, bool& isObject)
{
	isObject = false;
	containedInBlock = nullptr;
	if (ptr == nullptr)
	{
		// Shortcut
		return false;
	}

	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		// Equality is not valid (an object cannot be at the beginning of the heap nor at the very end)
		if (ptr > current->BlockStart && ptr < AddBytes(current->BlockStart, current->BlockSize))
		{
			// This pointer does point to an object in this block. Check that it points to a valid object start address
			BlockHd* hd = current->BlockStart;
			int offset = 0;
			int blockLen = current->BlockSize;
			while (offset < blockLen)
			{
				int entrySize = hd->BlockSize;

				void* objectPos = AddBytes(hd, ALLOCATE_ALLIGNMENT);

				if (objectPos == ptr)
				{
					// ptr points to the object stored in the block
					containedInBlock = hd;
					isObject = true;
					return true;
				}

				if (ptr >= objectPos && ptr < AddBytes(objectPos, entrySize))
				{
					containedInBlock = hd;
					isObject = false;
					return true;
				}

				hd = AddBytes(hd, entrySize + ALLOCATE_ALLIGNMENT);
				offset = offset + entrySize + ALLOCATE_ALLIGNMENT;
			}

			// Gets here when the value was within the range of this block, but apparently only by accident
			return false;
		}

		current = current->Next;
	}

	return false;
}

void GarbageCollector::MarkRawMemoryBlock(void* object, size_t objectSize, FirmataIlExecutor* referenceContainer)
{
	int* startPtr = (int*)object;
	for (size_t idx = 0; idx < objectSize / (sizeof(void*)); idx++)
	{
		int* ptrToTest = startPtr + idx;
		BlockHd* containedInBlock;
		bool isObject;
		bool test = IsValidMemoryObjectOrPointer((void*)*ptrToTest, containedInBlock, isObject);
		if (test && isObject)
		{
			Variable referenceField;
			referenceField.Marker = VARIABLE_DEFAULT_MARKER;
			referenceField.Type = VariableKind::Object;
			referenceField.setSize(sizeof(void*));
			// Create a variable object from a reference field that is stored in an object.
			// that way, we can continue recursively from this object
			referenceField.Object = (void*)*ptrToTest;
			MarkVariable(referenceField, referenceContainer);
		}
		else if (test)
		{
			// The pointer is possibly valid, but not an object. Assume we have some Span<T> pointing to the contents of something like a string
			containedInBlock->flags = BlockFlags::Used; // Mark as in use
		}
	}
}

/// <summary>
/// Mark the given variable as "not free"
/// </summary>
void GarbageCollector::MarkVariable(Variable& variable, FirmataIlExecutor* referenceContainer)
{
	// It seems we don't need to follow AddressOfVariable instances, since they always point to an otherwise accessible block
	if (variable.Type == VariableKind::Boolean || variable.Type == VariableKind::Double || variable.Type == VariableKind::Float || variable.Type == VariableKind::AddressOfVariable)
	{
		return;
	}
	if (variable.Type != VariableKind::ReferenceArray && /* variable.Type != VariableKind::AddressOfVariable &&*/
		variable.Type != VariableKind::Object && variable.Type != VariableKind::ValueArray)
	{
		// A value type (of any kind or length) - may contain pointers as well (we don't have full type info on these)
		// To make things simpler, value types which contain reference types have their fields pointer-aligned
		MarkRawMemoryBlock(&variable.Object, variable.fieldSize(), referenceContainer);
		return;
	}

	void* ptr = variable.Object;
	if (ptr == nullptr)
	{
		// Don't follow null pointers
		return;
	}
	
	BlockHd* hd = nullptr;
	GcBlock* current = _gcBlockHead;
	while (current != nullptr)
	{
		if (ptr < current->BlockStart || ptr > AddBytes(current->BlockStart, current->BlockSize))
		{
			// This pointer does not point to an object in this block.
			current = current->Next;
			continue;
		}

		hd = BlockHd::Cast(AddBytes(ptr, -((int32_t)ALLOCATE_ALLIGNMENT)));
		
		break;
	}

	if (hd == nullptr)
	{
		// Can't really happen, since when object is non-null, there must have been at least one element allocated.
		return;
	}

	if (hd->BlockSize == 0)
	{
		throw ExecutionEngineException("Memory block with size 0 found");
	}
	
	// Now ptr points to the object that is referenced and hd is its head
	if (!hd->IsFree())
	{
		// this is already marked as used - don't continue, or we could end in an infinite loop
		// if two objects contain a circular reference (besides that it would be a waste of performance)
		return;
	}
	
	hd->flags = BlockFlags::Used; // Mark as in use
	ClassDeclaration* cls = *(ClassDeclaration**)ptr;

	if (variable.Type == VariableKind::ReferenceArray)
	{
		int size = *AddBytes((int*)ptr, 4);
		Variable referenceField;
		referenceField.Marker = VARIABLE_DEFAULT_MARKER;
		referenceField.Type = VariableKind::Object;
		int arrayFieldType = *AddBytes((int*)ptr, 8);
		ClassDeclaration* elementTypes = referenceContainer->GetClassWithToken(arrayFieldType, false);
		if (elementTypes != nullptr)
		{
			if (elementTypes->IsValueType())
			{
				throw ClrException("Reference array containing value types?", SystemException::ArrayTypeMismatch, arrayFieldType);
			}
			if (elementTypes->ClassToken == (int)KnownTypeTokens::Array)
			{
				referenceField.Type = VariableKind::ReferenceArray;
			}
		}
		referenceField.setSize(4);

		for (int i = 0; i < size; i++)
		{
			referenceField.Object = *AddBytes((void**)ptr, ARRAY_DATA_START + i * sizeof(void*));
			MarkVariable(referenceField, referenceContainer);
		}

		return;
	}
	else if (variable.Type == VariableKind::ValueArray)
	{
		// The value types within the array could include further reference types, therefore try to extract that.
		// Luckily, here we know the type of the values
		int size = *AddBytes((int*)ptr, 4);
		int arrayFieldType = *AddBytes((int*)ptr, 8);
		Variable referenceField;
		ClassDeclaration* elementTypes = referenceContainer->GetClassWithToken(arrayFieldType, false);
		if (elementTypes == nullptr)
		{
			// We might not have the declaration for simple value types
			return;
		}
		referenceField.Marker = VARIABLE_DEFAULT_MARKER;
		referenceField.setSize(4);

		for (int i = 0; i < size; i++)
		{
			void* elemStart = AddBytes(ptr, ARRAY_DATA_START + i * elementTypes->ClassDynamicSize);
			int offset = 0;
			int idx = 0;
			for (auto handle = elementTypes->GetFieldByIndex(idx); handle != nullptr; handle = elementTypes->GetFieldByIndex(++idx))
			{
				if ((handle->Type & VariableKind::StaticMember) != VariableKind::Void)
				{
					continue;
				}
				if (handle->Type == VariableKind::Object || handle->Type == VariableKind::ReferenceArray || handle->Type == VariableKind::ValueArray)
				{
					referenceField.Type = handle->Type;
					referenceField.Object = (void*)*AddBytes((int*)elemStart, offset);
					MarkVariable(referenceField, referenceContainer);
				}

				offset += handle->fieldSize();
			}
		}

		return;
	}

	// Iterate over the fields of a class
	int offset = sizeof(void*);
	VariableIterator it;
	Variable* handle = nullptr;
	while ((handle = referenceContainer->CollectFields(cls, it)))
	{
		// Ignore static member here
		if ((handle->Type & VariableKind::StaticMember) != VariableKind::Void)
		{
			continue;
		}

		if (!handle->isValueType())
		{
			// TODO: Quite ugly that we have to do a full variable extraction to follow the reference chain
			Variable referenceField;
			referenceField.Marker = VARIABLE_DEFAULT_MARKER;
			referenceField.Type = handle->Type;
			referenceField.setSize(4);
			referenceField.Object = (void*)*AddBytes((int*)ptr, offset);
			MarkVariable(referenceField, referenceContainer);
		}
		else
		{
			int size = handle->fieldSize();
			if (size >= sizeof(void*))
			{
				// Could this member be an object reference?
				void* potentiallyAnObject = (void*) *AddBytes((int*)ptr, offset);
				BlockHd* containedInBlock;
				bool isObject;
				// This tests whether it's really pointing to a valid object start address
				if (IsValidMemoryObjectOrPointer(potentiallyAnObject, containedInBlock, isObject))
				{
					hd = BlockHd::Cast(AddBytes(potentiallyAnObject, -((int32_t)ALLOCATE_ALLIGNMENT)));
					hd->flags = BlockFlags::Used;
					MarkRawMemoryBlock(potentiallyAnObject, handle->fieldSize(), referenceContainer);
				}
			}
		}
		
		offset += handle->fieldSize();
	}

	/*
	while ((fieldType = cls->GetFieldByIndex(idx)) != nullptr)
	{
		if ((fieldType->Type & VariableKind::StaticMember) != VariableKind::Void)
		{
			idx++;
			continue;
		}
		if (fieldType->Type == VariableKind::Object || fieldType->Type == VariableKind::ReferenceArray || fieldType->Type == VariableKind::ValueArray)
		{
			Variable referenceField;
			referenceField.Marker = VARIABLE_DEFAULT_MARKER;
			referenceField.Type = fieldType->Type;
			referenceField.setSize(4);
			referenceField.Object = (void*)*AddBytes((int*)ptr, offset);
			MarkVariable(referenceField, referenceContainer);
		}

		offset += fieldType->fieldSize();
		idx++;
	}
	*/
}
