﻿
#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "GarbageCollector.h"
#include "SelfTest.h"

void GarbageCollector::Init(FirmataIlExecutor* referenceContainer)
{
	// this performs a GC self-test
	byte* first = Allocate(20);
	byte* second = Allocate(31);
	byte* third = Allocate(40);
	memset(first, 1, 20);
	memset(second, 2, 31);
	memset(third, 3, 40);

	// This line is machine-specific, but currently helps in verifying the size of the BlockHd structure
	ASSERT(ALLOCATE_ALLIGNMENT == 4);
	ValidateBlocks();
	first = second = third = nullptr;
	int collected = Collect(0, referenceContainer);
	ASSERT(collected > 90);
	ValidateBlocks();
}


byte* GarbageCollector::Allocate(uint32_t size)
{
	byte* ret = nullptr;
	TRACE(Firmata.sendStringf(F("Allocating %d bytes"), 4, size));
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
			// Still null? That's bad. Either the memory is completely exhausted, or the requested block is just way to large
			OutOfMemoryException::Throw("Out of memory increasing GC controlled memory");
		}
		
		GcBlock block;
		block.BlockSize = sizeToAllocate;
		block.BlockStart = (BlockHd*)newBlockPtr;
		block.FreeBytesInBlock = (uint16_t)(sizeToAllocate - ALLOCATE_ALLIGNMENT);
		block.Tail = block.BlockStart;
		BlockHd::SetBlockAtAddress(newBlockPtr, block.FreeBytesInBlock, BlockFlags::Free);
		
		_gcBlocks.push_back(block);

		ret = TryAllocateFromBlock(_gcBlocks.back(), size);
	}

	ValidateBlocks();
	_totalAllocSize += size;
	_totalAllocations++;

	TRACE(Firmata.sendStringf(F("Address is 0x%lx"), 4, ret));

	ASSERT(((uint32_t)ret % ALLOCATE_ALLIGNMENT) == 0);

	BlockHd* hd = BlockHd::Cast(ret - (int32_t)ALLOCATE_ALLIGNMENT);
	ASSERT(!hd->IsFree());
	ASSERT(hd->Marker = BLOCK_MARKER);
	
	return ret;
}

void GarbageCollector::ValidateBlocks()
{
	for (size_t i = 0; i < _gcBlocks.size(); i++)
	{
		GcBlock& block = _gcBlocks[i];
		int blockLen = block.BlockSize;
		int offset = 0;
		BlockHd* hd = block.BlockStart;
		while (offset < blockLen)
		{
			int size = hd->BlockSize;
			if (size <= 0)
			{
				throw ExecutionEngineException("Invalid block size in memory list. That shouldn't happen.");
			}
			
			if (size > blockLen)
			{
				// A single entry cannot be larger than the memory block - something is wrong.
				throw ExecutionEngineException("Block list inconsistent");
			}

			if (hd->Marker != BLOCK_MARKER)
			{
				throw ExecutionEngineException("Block marker missing.");
			}
			
			hd = AddBytes(hd, size + ALLOCATE_ALLIGNMENT);
			offset = offset + size + ALLOCATE_ALLIGNMENT;
		}

		// At the end, the last block must end exactly at the block end.
		if (offset != blockLen)
		{
			throw ExecutionEngineException("Memory list inconsistent");
		}
	}
}

byte* GarbageCollector::TryAllocateFromBlock(GcBlock& block, uint32_t size)
{
	if (size == 0)
	{
		// Allocating empty blocks always returns the same address (that's probably never going to happen, since
		// every object at least has a vtable)
		return (byte*)block.BlockStart;
	}

	if (size > block.FreeBytesInBlock)
	{
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
	if (block.Tail + realSizeToReserve + ALLOCATE_ALLIGNMENT < AddBytes(block.BlockStart, block.BlockSize))
	{
		// There's room at the end of the block. Just use this.
		hd = block.Tail;
		uint16_t availableToEnd = hd->BlockSize;
		ASSERT(hd->flags == BlockFlags::Free && (availableToEnd >= realSizeToReserve));
		hd->BlockSize = (uint16_t)realSizeToReserve;
		hd->flags = BlockFlags::Used;
		ret = (byte*)AddBytes(hd, ALLOCATE_ALLIGNMENT);
		hd = AddBytes(hd, ALLOCATE_ALLIGNMENT + realSizeToReserve);
		BlockHd::SetBlockAtAddress(hd, availableToEnd - (int)realSizeToReserve - ALLOCATE_ALLIGNMENT, BlockFlags::Free); // It's free memory
		block.Tail = hd;
		block.FreeBytesInBlock -= realSizeToReserve + ALLOCATE_ALLIGNMENT;
		return ret;
	}

	// If we get here, the block has been filled for the first time, therefore move it's tail to the end
	block.Tail = block.BlockStart + block.BlockSize;
	// There's not enough room at the end of the block. Check whether we find a place within the block
	hd = block.BlockStart;
	while (hd < AddBytes(block.BlockStart,block.BlockSize) && hd->BlockSize != 0)
	{
		uint16_t thisBlockSize = hd->BlockSize;
		if (!hd->IsFree())
		{
			hd = AddBytes(hd, (thisBlockSize + ALLOCATE_ALLIGNMENT));
			continue;
		}
		
		if (thisBlockSize >= realSizeToReserve && thisBlockSize <= 2 * realSizeToReserve)
		{
			if (realSizeToReserve >= thisBlockSize + ALLOCATE_ALLIGNMENT)
			{
				// Split up the new block (but make sure we don't split away a 0-byte block)
				ret = (byte*)AddBytes(hd, ALLOCATE_ALLIGNMENT);
				hd->BlockSize = realSizeToReserve;
				hd->flags = BlockFlags::Used;
				hd = AddBytes(hd, ALLOCATE_ALLIGNMENT + realSizeToReserve);
				BlockHd::SetBlockAtAddress(hd, thisBlockSize - ALLOCATE_ALLIGNMENT - realSizeToReserve, BlockFlags::Free);
				
				block.FreeBytesInBlock -= realSizeToReserve + ALLOCATE_ALLIGNMENT;
				return ret;
			}
			ret = (byte*)AddBytes(hd, ALLOCATE_ALLIGNMENT);
			hd->flags = BlockFlags::Used; // Reserve the whole block
			block.FreeBytesInBlock -= thisBlockSize + ALLOCATE_ALLIGNMENT;
			return ret;
		}
		hd = AddBytes(hd, (thisBlockSize + ALLOCATE_ALLIGNMENT));
	}

	return nullptr;
}

void GarbageCollector::PrintStatistics()
{
	Firmata.sendStringf(F("Total GC memory allocated: %d bytes in %d instances"), 8, _totalAllocSize, _totalAllocations);
	Firmata.sendStringf(F("Current/Maximum GC memory used: %d/%d bytes"), 8, _currentMemoryUsage, _maxMemoryUsage);
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
	
	_totalAllocSize = 0;
	_totalAllocations = 0;
	_currentMemoryUsage = 0;
	_maxMemoryUsage = 0;
}

/// <summary>
/// Return the total size of the memory in the GC blocks.
/// </summary>
int64_t GarbageCollector::AllocatedMemory()
{
	int64_t memorySum = 0;
	for (size_t idx1 = 0; idx1 < _gcBlocks.size(); idx1++)
	{
		memorySum += _gcBlocks[idx1].BlockSize;
	}

	return memorySum;
}

void GarbageCollector::MarkAllFree()
{
	for (size_t idx1 = 0; idx1 < _gcBlocks.size(); idx1++)
	{
		MarkAllFree(_gcBlocks[idx1]);
	}
}

void GarbageCollector::MarkAllFree(GcBlock& block)
{
	int blockLen = block.BlockSize;
	int offset = 0;
	BlockHd* hd = block.BlockStart;
	while (offset < blockLen)
	{
		int blockSize = hd->BlockSize;

		hd->flags = BlockFlags::Free;

		hd = AddBytes(hd, blockSize + ALLOCATE_ALLIGNMENT);
		offset = offset + blockSize + ALLOCATE_ALLIGNMENT;
	}
}

int GarbageCollector::ComputeFreeBlockSizes()
{
	int totalFreed = 0;
	int totalMemoryInUse = 0;
	for (size_t idx = 0; idx < _gcBlocks.size(); idx++)
	{
		int blockLen = _gcBlocks[idx].BlockSize;
		int offset = 0;
		BlockHd* hd = _gcBlocks[idx].BlockStart;
		int blockFree = 0;
		while (offset < blockLen)
		{
			int entryLength = hd->BlockSize;

			if (hd->IsFree())
			{
				blockFree += entryLength;
			}
			else
			{
				totalMemoryInUse += entryLength;
			}

			hd = AddBytes(hd, entryLength + ALLOCATE_ALLIGNMENT);
			offset = offset + entryLength + ALLOCATE_ALLIGNMENT;
		}
		if (blockFree > _gcBlocks[idx].FreeBytesInBlock)
		{
			totalFreed += blockFree - _gcBlocks[idx].FreeBytesInBlock;
			_gcBlocks[idx].FreeBytesInBlock = (short)blockFree;
		}
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
	TRACE(Firmata.sendString(F("Beginning GC")));
	MarkAllFree();
	MarkStatics(referenceContainer);
	MarkStack(referenceContainer);
	int result = ComputeFreeBlockSizes();
	TRACE(Firmata.sendString(F("GC done")));
	return result;
}

void GarbageCollector::MarkStatics(FirmataIlExecutor* referenceContainer)
{
	auto& values = referenceContainer->_statics.values();

	for (size_t i = 0; i < values.size(); i++)
	{
		Variable& var = values.at(i);
		MarkVariable(var, referenceContainer);
	}

	VariableListEntry* e = referenceContainer->_largeStatics.first();
	while (e != nullptr)
	{
		Variable& ref = e->Data;
		MarkVariable(ref, referenceContainer);
		e = referenceContainer->_largeStatics.next(e);
	}
}

void GarbageCollector::MarkStack(FirmataIlExecutor* referenceContainer)
{
	ExecutionState* state = referenceContainer->_methodCurrentlyExecuting;
	
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

		state = state->_next;
	}
}

/// <summary>
/// Tests whether the given pointer could be an object pointer (means that it contains a value that could be an address in our heap)
/// </summary>
bool GarbageCollector::IsValidMemoryPointer(void* ptr)
{
	for (size_t idx1 = 0; idx1 < _gcBlocks.size(); idx1++)
	{
		// Equality is not valid (an object cannot be at the beginning of the heap nor at the very end)
		if (ptr > _gcBlocks[idx1].BlockStart && ptr < AddBytes(_gcBlocks[idx1].BlockStart, _gcBlocks[idx1].BlockSize))
		{
			// This pointer does point to an object in this block.
			return true;
		}
	}

	return false;
}

/// <summary>
/// Mark the given variable as "not free"
/// </summary>
void GarbageCollector::MarkVariable(Variable& variable, FirmataIlExecutor* referenceContainer)
{
	// TODO: Check whether we also need to consider variables of type int (as they might actually be IntPtr types)
	// It seems we don't need to follow AddressOfVariable instances, since they always point to an otherwise accessible block
	if (variable.Type == VariableKind::Boolean || variable.Type == VariableKind::Double || variable.Type == VariableKind::Float || variable.Type == VariableKind::AddressOfVariable)
	{
		return;
	}
	if (variable.Type != VariableKind::ReferenceArray && /* variable.Type != VariableKind::AddressOfVariable &&*/
		variable.Type != VariableKind::Object && variable.Type != VariableKind::ValueArray)
	{
		// A value type - but may contain pointers as well (we don't have full type info on these)
		// To make things simpler, value types which contain reference types have their fields pointer-aligned
		int* startPtr = (int*)&variable.Object;
		for (size_t idx = 0; idx < variable.fieldSize() / (sizeof(void*)); idx++)
		{
			int* ptrToTest = startPtr + idx;
			if (IsValidMemoryPointer((void*)*ptrToTest))
			{
				Variable referenceField;
				referenceField.Marker = VARIABLE_DEFAULT_MARKER;
				referenceField.Type = VariableKind::AddressOfVariable;
				referenceField.setSize(sizeof(void*));
				// Create a variable object from a reference field that is stored in an object
				referenceField.Object = (void*)*ptrToTest;
				MarkVariable(referenceField, referenceContainer);
			}
		}
		return;
	}

	void* ptr = variable.Object;
	if (ptr == nullptr)
	{
		// Don't follow null pointers
		return;
	}
	
	BlockHd* hd = nullptr;
	for (size_t idx1 = 0; idx1 < _gcBlocks.size(); idx1++)
	{
		if (ptr < _gcBlocks[idx1].BlockStart || ptr > AddBytes(_gcBlocks[idx1].BlockStart, _gcBlocks[idx1].BlockSize))
		{
			// This pointer does not point to an object in this block.
			continue;
		}

		GcBlock& block = _gcBlocks[idx1];
		hd = BlockHd::Cast(AddBytes(ptr, -((int32_t)ALLOCATE_ALLIGNMENT)));
		if (variable.Type == VariableKind::AddressOfVariable)
		{
			// This is a managed pointer, it can point to within an object, so we need to scan the list
			hd = block.BlockStart;
			int offset = 0;
			int blockLen = block.BlockSize;
			while (offset < blockLen)
			{
				int entrySize = hd->BlockSize;

				if (ptr > hd && ptr < AddBytes(hd, entrySize + ALLOCATE_ALLIGNMENT))
				{
					break;
				}

				hd = AddBytes(hd, entrySize + ALLOCATE_ALLIGNMENT);
				offset = offset + entrySize + ALLOCATE_ALLIGNMENT;
			}
		}
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
			if (elementTypes->ValueType)
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
	int idx = 0;
	Variable* fieldType = nullptr;
	int offset = sizeof(void*);
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
}
