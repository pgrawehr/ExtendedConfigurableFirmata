#include <ConfigurableFirmata.h>

#include "SimFlashStorage.h"
#include "Variable.h"


VirtualFlashMemory::VirtualFlashMemory(size_t size)
{
	_memoryBasePtr = (byte*)malloc(size);
	memset(_memoryBasePtr, -1, size); // The Arduino Due's flash is initialized to all 1's when erasing
}

VirtualFlashMemory::~VirtualFlashMemory()
{
	if (_memoryBasePtr != nullptr)
	{
		free(_memoryBasePtr);
	}

	_memoryBasePtr = nullptr;
}


byte* VirtualFlashMemory::readAddress(uint32_t address)
{
	return AddBytes(_memoryBasePtr, address);
}

byte* VirtualFlashMemory::getFirstFreeBlock()
{
	return (byte*)_memoryBasePtr;
}


boolean VirtualFlashMemory::write(uint32_t address, byte* data, uint32_t dataLength)
{
	memcpy(_memoryBasePtr + address, data, dataLength);
	return true;
}

boolean VirtualFlashMemory::write(byte* address, byte* data, uint32_t dataLength)
{
	return write(address - _memoryBasePtr, data, dataLength);
}
