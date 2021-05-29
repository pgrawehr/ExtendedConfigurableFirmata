// 
// 
// 

#include <ConfigurableFirmata.h>
#ifdef ESP32
#include "Esp32FlashStorage.h"
Esp32CliFlashStorage* storage;
#elif SIM
#include "SimFlashStorage.h"
VirtualFlashMemory* storage;
#elif __SAM3X8E__
#include <DueFlashStorage.h>
DueFlashStorage* storage;
#elif
	// TODO: Create a dummy storage driver (with zero bytes size)
#error No storage driver available
#endif

#include "FlashMemoryManager.h"
#include "Variable.h"
#include "Exceptions.h"

using namespace stdSimple;

const int FLASH_MEMORY_IDENTIFIER = 0x7AABCDBB;
const int MEMORY_ALLOCATION_ALIGNMENT = 4;

struct FlashMemoryHeader
{
public:
	int Identifier;
	int DataVersion;
	int DataHashCode;
	void* Classes;
	void* Methods;
	void* Constants;
	void* StringHeap;
	byte* EndOfHeap;
	int* SpecialTokenList;
	
	int StartupToken;
	// Bit 0: Auto-Restart task after crash
	int StartupFlags;
};

FlashMemoryManager::FlashMemoryManager()
{
#ifdef ESP32
	storage = new Esp32CliFlashStorage();
#elif SIM
	storage = new VirtualFlashMemory(1024 * 1024);
#elif __SAM3X8E__
	storage = new DueFlashStorage();
#elif
	// TODO: Create a dummy storage driver (with zero bytes size)
#error No storage driver available
#endif
	_endOfHeap = _startOfHeap = storage->getFirstFreeBlock(); // This just returns the start of the flash memory used by this manager
	_flashEnd = storage->readAddress(0) + storage->getFlashSize();
	_header = (FlashMemoryHeader*)_startOfHeap;
	_endOfHeap = AddBytes(_endOfHeap, (sizeof(FlashMemoryHeader) + MEMORY_ALLOCATION_ALIGNMENT) & ~(MEMORY_ALLOCATION_ALIGNMENT - 1));
	_headerClear = true;
	if (_header->Identifier == FLASH_MEMORY_IDENTIFIER && _header->DataVersion != -1 && _header->DataVersion != 0)
	{
		_endOfHeap = _header->EndOfHeap;
		_headerClear = false;
	}
}

void FlashMemoryManager::Init(void*& classes, void*& methods, void*& constants, void*& stringHeap, int*& specialTokenList, int& startupToken, int& startupFlags)
{
	if (_header->Identifier == FLASH_MEMORY_IDENTIFIER && _header->DataVersion != -1 && _header->DataVersion != 0)
	{
		_endOfHeap = _header->EndOfHeap;
		_headerClear = false;
		classes = _header->Classes;
		methods = _header->Methods;
		constants = _header->Constants;
		stringHeap = _header->StringHeap;
		startupToken = _header->StartupToken;
		startupFlags = _header->StartupFlags;
		specialTokenList = _header->SpecialTokenList;
	}
	else
	{
		classes = nullptr;
		methods = nullptr;
		constants = nullptr;
		stringHeap = nullptr;
		startupToken = 0;
		startupFlags = 0;
		specialTokenList = nullptr;
	}
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
	storage->eraseBlock(storage->getOffset(_startOfHeap), _flashEnd - _startOfHeap);
}

void* FlashMemoryManager::FlashAlloc(size_t bytes)
{
	if (_endOfHeap + bytes + MEMORY_ALLOCATION_ALIGNMENT >= _flashEnd)
	{
		size_t free = _flashEnd - _endOfHeap;
		Firmata.sendStringf(F("Not enough flash to reserve %li bytes, only %li free"), 4, bytes, free);
		OutOfMemoryException::Throw("Out of flash memory");
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

	if (!storage->write((byte*)flashTarget, (byte*)src, length))
	{
		throw ExecutionEngineException("Error writing flash");
	}
}

void FlashMemoryManager::WriteHeader(int dataVersion, int hashCode, void* classesPtr, void* methodsPtr, void* constantsPtr, void* stringHeapPtr, int* specialTokenList, int startupToken, int startupFlags)
{
	FlashMemoryHeader hd;
	hd.DataVersion = dataVersion;
	hd.DataHashCode = hashCode;
	hd.EndOfHeap = _endOfHeap;
	hd.Identifier = FLASH_MEMORY_IDENTIFIER;
	hd.Classes = classesPtr;
	hd.Methods = methodsPtr;
	hd.Constants = constantsPtr;
	hd.StringHeap = stringHeapPtr;
	hd.SpecialTokenList = specialTokenList;
	hd.StartupToken = startupToken;
	hd.StartupFlags = startupFlags;
	
	storage->write(_startOfHeap, (byte*)&hd, sizeof(FlashMemoryHeader));
	_headerClear = false;

	int bytesUsed = _endOfHeap - _startOfHeap;
	int bytesTotal = _flashEnd - _startOfHeap;
	Firmata.sendStringf(F("Flash data written: %d bytes of %d used."), 4, bytesUsed, bytesTotal);
}

