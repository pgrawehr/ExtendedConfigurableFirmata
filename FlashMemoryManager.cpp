// 
// 
// 

#include <ConfigurableFirmata.h>
#ifndef SIM
#include <DueFlashStorage.h>
DueFlashStorage storage;
#else
#include "SimFlashStorage.h"
#endif

#include "FlashMemoryManager.h"
#include "Exceptions.h"

using namespace stdSimple;

FlashMemoryManager FlashManager;

FlashMemoryManager::FlashMemoryManager()
{
	_endOfHeap = _startOfHeap = storage.readAddress(0); // This just returns the start of the flash memory used by this manager
	_flashEnd = _startOfHeap + IFLASH1_SIZE;
}

void FlashMemoryManager::Clear()
{
	_endOfHeap = _startOfHeap;
}

void* FlashMemoryManager::FlashAlloc(size_t bytes)
{
	byte* ret = _endOfHeap;
	// Keep heap addresses 8-byte aligned
	if (bytes % 8 != 0)
	{
		bytes = (bytes + 8) & ~0x7;
	}
	_endOfHeap += bytes;
	return ret;
}

void FlashMemoryManager::CopyToFlash(void* src, void* flashTarget, size_t length)
{
	if (length == 0)
	{
		return;
	}
	
	if (!((flashTarget >= _startOfHeap) && (flashTarget < _endOfHeap)))
	{
		// This is not a valid target address
		throw ExecutionEngineException("Flash memory address out of bounds");
	}

	if (!storage.write((uint32_t)flashTarget, (byte*)src, length))
	{
		throw ExecutionEngineException("Error writing flash");
	}
}



