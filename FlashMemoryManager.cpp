// 
// 
// 

#include <ConfigurableFirmata.h>
#include "ArduinoDueSupport.h"

#ifdef ESP32
#include "Esp32FlashStorage.h"
Esp32CliFlashStorage* storage;
#elif SIM
#include "SimFlashStorage.h"
VirtualFlashMemory* storage;
#elif defined ARDUINO_DUE
#include <DueFlashStorage.h>
DueFlashStorage* storage;

#else
	// TODO: Create a dummy storage driver (with zero bytes size)
#error No storage driver available
#endif

#include "FlashMemoryManager.h"
#include "Variable.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace stdSimple;

const int FLASH_MEMORY_IDENTIFIER = 0x7AABCDBB;
const int MEMORY_ALLOCATION_ALIGNMENT = 4;

const int TIMESTAMP_SIZE = 30;
struct FlashMemoryHeader
{
public:
	int Identifier;
	int DataVersion;
	int DataHashCode;
	void* Classes;
	void* Methods;
	void* Constants;
	void* Clauses;
	void* StringHeap;
	byte* EndOfHeap;
	int* SpecialTokenList;
	
	int StartupToken;
	// Bit 0: Auto-Restart task after crash
	int StartupFlags;

	uint32_t StaticVectorMemorySize;

	// These two are used to check that the contents of the flash memory matches the firmware.
	// Since we're storing objects that include code references (vtables) in flash, updating the build invalidates it.
	char FirmwareBuildTime[TIMESTAMP_SIZE];
};

FlashMemoryManager::FlashMemoryManager()
{
#ifdef ESP32
	storage = new Esp32CliFlashStorage();
	storage->MapFlash();
#elif SIM
	storage = new VirtualFlashMemory(1024 * 1024);
#elif __SAM3X8E__
	storage = new DueFlashStorage();
#else
	// TODO: Create a dummy storage driver (with zero bytes size)
#error No storage driver available
#endif
	InitHeader();
}

bool FlashMemoryManager::InitHeader()
{
	_endOfHeap = _startOfHeap = storage->getFirstFreeBlock(); // This just returns the start of the flash memory used by this manager
	_flashEnd = storage->readAddress(0) + storage->getFlashSize();
	_header = (FlashMemoryHeader*)_startOfHeap;
	_endOfHeap = AddBytes(_endOfHeap, (sizeof(FlashMemoryHeader) + MEMORY_ALLOCATION_ALIGNMENT) & ~(MEMORY_ALLOCATION_ALIGNMENT - 1));
	_headerClear = true;
	_flashClear = false;
	if (_header->Identifier == FLASH_MEMORY_IDENTIFIER && _header->DataVersion != -1 && _header->DataVersion != 0)
	{
		_endOfHeap = _header->EndOfHeap;
		_headerClear = false;
		return true;
	}

	return false;
}

long FlashMemoryManager::TotalFlashMemory() const
{
	return storage->getFlashSize();
}

long FlashMemoryManager::UsedFlashMemory()
{
	return _endOfHeap - _startOfHeap;
}



void FlashMemoryManager::Init(void*& classes, void*& methods, void*& constants, void*& stringHeap, int*& specialTokenList,
	void*& clauses, int& startupToken, int& startupFlags, uint32_t& staticVectorMemorySize)
{
	bool tryRead = ValidateFlashContents();
	if (tryRead && _header->DataVersion != -1 && _header->DataVersion != 0)
	{
		_endOfHeap = _header->EndOfHeap;
		_headerClear = false;
		classes = _header->Classes;
		methods = _header->Methods;
		constants = _header->Constants;
		clauses = _header->Clauses;
		stringHeap = _header->StringHeap;
		startupToken = _header->StartupToken;
		startupFlags = _header->StartupFlags;
		specialTokenList = _header->SpecialTokenList;
		staticVectorMemorySize = _header->StaticVectorMemorySize;
	}
	else
	{
		classes = nullptr;
		methods = nullptr;
		constants = nullptr;
		stringHeap = nullptr;
		clauses = nullptr;
		startupToken = 0;
		startupFlags = 0;
		specialTokenList = nullptr;
		staticVectorMemorySize = 0;
	}
}

bool FlashMemoryManager::ValidateFlashContents() const
{
	if (_header->Identifier != FLASH_MEMORY_IDENTIFIER)
	{
		return false;
	}

	if (strncmp(__TIMESTAMP__, _header->FirmwareBuildTime, TIMESTAMP_SIZE) != 0)
	{
		return false;
	}

	return true;
}


bool FlashMemoryManager::ContainsMatchingData(int dataVersion, int hashCode)
{
	if (_headerClear)
	{
		return false;
	}
	
	if (ValidateFlashContents() && _header->DataVersion == dataVersion && _header->DataHashCode == hashCode)
	{
		Firmata.sendString(F("Found matching data in flash."));
		return true;
	}

	return false;
}

void FlashMemoryManager::Clear()
{
	if (!_flashClear)
	{
		_endOfHeap = _startOfHeap;
		size_t flashPageSize = storage->getFlashPageSize();
		if (sizeof(FlashMemoryHeader) > flashPageSize)
		{
			Firmata.sendStringf(F("Flash header to big"));
		}
		_endOfHeap = AddBytes(_endOfHeap, flashPageSize);
		_headerClear = true;
		// storage->UnmapFlash();
		storage->eraseBlock(0, storage->getFlashSize());
	}
	_flashClear = true;
}

void* FlashMemoryManager::FlashAlloc(size_t bytes)
{
	if (_endOfHeap + bytes + MEMORY_ALLOCATION_ALIGNMENT >= _flashEnd)
	{
		size_t free = _flashEnd - _endOfHeap;
		Firmata.sendStringf(F("Not enough flash to reserve %li bytes, only %li free"), bytes, free);
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

void FlashMemoryManager::CopyToFlash(void* src, void* flashTarget, size_t length, const char* usage)
{
	Firmata.sendStringf(F("Flashing block for %s"), usage);
	_flashClear = false;
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

void FlashMemoryManager::WriteHeader(int dataVersion, int hashCode, void* classesPtr, void* methodsPtr, void* constantsPtr,
	void* stringHeapPtr, int* specialTokenList, void* clauses, int startupToken, int startupFlags, int staticVectorMemorySize)
{
	_flashClear = false;
	FlashMemoryHeader hd;
	memset(&hd, 0, sizeof(FlashMemoryHeader));
	hd.DataVersion = dataVersion;
	hd.DataHashCode = hashCode;
	hd.EndOfHeap = _endOfHeap;
	hd.Identifier = FLASH_MEMORY_IDENTIFIER;
	hd.Classes = classesPtr;
	hd.Clauses = clauses;
	hd.Methods = methodsPtr;
	hd.Constants = constantsPtr;
	hd.StringHeap = stringHeapPtr;
	hd.SpecialTokenList = specialTokenList;
	hd.StartupToken = startupToken;
	hd.StartupFlags = startupFlags;
	hd.StaticVectorMemorySize = staticVectorMemorySize;
	strncpy_s(hd.FirmwareBuildTime, TIMESTAMP_SIZE, __TIMESTAMP__, _TRUNCATE);
	
	storage->write(_startOfHeap, (byte*)&hd, sizeof(FlashMemoryHeader));
	// storage->MapFlash(); // All done -> Map again
	_headerClear = false;
	bool success = InitHeader();

	int bytesUsed = _endOfHeap - _startOfHeap;
	int bytesTotal = _flashEnd - _startOfHeap;
	Firmata.sendStringf(F("Flash data written: %d bytes of %d used."), bytesUsed, bytesTotal);
	if (success)
	{
		Firmata.sendStringf(F("Data appears to be valid now"));
	}
	else
	{
		Firmata.sendStringf(F("Remapping flash didn't work, no valid header at address 0x%x"), storage->readAddress(0));
	}
}
