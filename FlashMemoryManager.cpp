// 
// 
// 

#include <ConfigurableFirmata.h>
#ifndef SIM
#include <DueFlashStorage.h>
DueFlashStorage* storage = nullptr;
#else
#include "SimFlashStorage.h"
VirtualFlashMemory* storage = nullptr;
#endif

#include "FlashMemoryManager.h"
#include "Variable.h"
#include "Exceptions.h"

using namespace stdSimple;

FlashMemoryManager FlashManager;

const int FLASH_MEMORY_IDENTIFIER = 0x7AABCDBB;
const int MEMORY_ALLOCATION_ALIGNMENT = 4;

struct FlashMemoryHeader
{
public:
	int Identifier;
	int DataVersion;
	int DataHashCode;
	byte* EndOfHeap;
};

FlashMemoryManager::FlashMemoryManager()
{
#ifdef SIM
	storage = new VirtualFlashMemory(IFLASH1_SIZE);
#else
	storage = new DueFlashStorage();
#endif
	_endOfHeap = _startOfHeap = storage->readAddress(0); // This just returns the start of the flash memory used by this manager
	_flashEnd = _startOfHeap + IFLASH1_SIZE;
	_header = (FlashMemoryHeader*)_startOfHeap;
	_endOfHeap = AddBytes(_endOfHeap, (sizeof(FlashMemoryHeader) + MEMORY_ALLOCATION_ALIGNMENT) & ~(MEMORY_ALLOCATION_ALIGNMENT - 1));
	_headerClear = false;
	Init();
}

void FlashMemoryManager::Init()
{
	if (_header->Identifier == FLASH_MEMORY_IDENTIFIER && _header->DataVersion != -1 && _header->DataVersion != 0)
	{
		_endOfHeap = _header->EndOfHeap;
		_headerClear = false;
	}

	// TODO: Read remainder of data structure
}

bool FlashMemoryManager::ContainsMatchingData(int dataVersion, int hashCode)
{
	if (_headerClear)
	{
		return false;
	}
	
	if (_header->Identifier == FLASH_MEMORY_IDENTIFIER && _header->DataVersion == dataVersion && _header->DataHashCode == hashCode)
	{
		Firmata.sendString(F("Found matching data in flash."));
		return true;
	}

	return false;
}

void FlashMemoryManager::Clear()
{
	_endOfHeap = _startOfHeap;
	_endOfHeap = AddBytes(_endOfHeap, (sizeof(FlashMemoryHeader) + MEMORY_ALLOCATION_ALIGNMENT) & ~(MEMORY_ALLOCATION_ALIGNMENT - 1));
	_headerClear = true;
}

void* FlashMemoryManager::FlashAlloc(size_t bytes)
{
	if (_endOfHeap + bytes + MEMORY_ALLOCATION_ALIGNMENT >= _flashEnd)
	{
		OutOfMemoryException::Throw();
	}
	
	byte* ret = _endOfHeap;
	// Keep heap addresses aligned
	if (bytes % MEMORY_ALLOCATION_ALIGNMENT != 0)
	{
		bytes = (bytes + MEMORY_ALLOCATION_ALIGNMENT) & ~(MEMORY_ALLOCATION_ALIGNMENT - 1);
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

	if (!storage->write((byte*)flashTarget - _startOfHeap, (byte*)src, length))
	{
		throw ExecutionEngineException("Error writing flash");
	}
}

void FlashMemoryManager::WriteHeader(int dataVersion, int hashCode)
{
	FlashMemoryHeader hd;
	hd.DataVersion = dataVersion;
	hd.DataHashCode = hashCode;
	hd.EndOfHeap = _endOfHeap;
	hd.Identifier = FLASH_MEMORY_IDENTIFIER;
	storage->write(0, (byte*)&hd, sizeof(FlashMemoryHeader));
	_headerClear = false;

	int bytesUsed = _endOfHeap - _startOfHeap;
	int bytesTotal = _flashEnd - _startOfHeap;
	Firmata.sendStringf(F("Flash data written: %d bytes of %d used."), 4, bytesUsed, bytesTotal);
}

