#include <ConfigurableFirmata.h>

#include "SimFlashStorage.h"
#include "Variable.h"


VirtualFlashMemory::VirtualFlashMemory(size_t size)
{
	_memoryBasePtr = (byte*)malloc(size);
	memset(_memoryBasePtr, -1, size); // The Arduino Due's flash is initialized to all 1's when erasing
}


byte* VirtualFlashMemory::readAddress(uint32_t address)
{
	return AddBytes(_memoryBasePtr, address);
}

boolean VirtualFlashMemory::write(uint32_t address, byte* data, uint32_t dataLength)
{
	memcpy((void*)address, data, dataLength);
	return true;
}
