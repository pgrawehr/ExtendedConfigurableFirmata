#pragma once

#include <ConfigurableFirmata.h>

#ifdef SIM

#define IFLASH_ADDR 0
#define IFLASH1_SIZE (256 * 1024)
#endif

// This class simulates flash by memory. This is not within the #ifdef SIM to simplify development.
class VirtualFlashMemory
{
private:
	byte* _memoryBasePtr;

public:
	VirtualFlashMemory(size_t size);

	~VirtualFlashMemory();

	byte* readAddress(uint32_t address);

	/// <summary>
	/// Write a block to flash
	/// </summary>
	/// <param name="address">Address, relative to start of flash</param>
	/// <param name="data">Pointer to data</param>
	/// <param name="dataLength">Length of data</param>
	/// <returns>True on success, false otherwise</returns>
	boolean write(uint32_t address, byte* data, uint32_t dataLength);
};

