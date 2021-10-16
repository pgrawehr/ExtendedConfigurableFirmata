#pragma once

#include <ConfigurableFirmata.h>

// This class simulates flash by memory. This is not within the #ifdef SIM to simplify development.
class VirtualFlashMemory
{
private:
	byte* _memoryBasePtr;
	uint32_t _memorySize;

public:
	VirtualFlashMemory(size_t size);

	~VirtualFlashMemory();

	byte* readAddress(uint32_t address);

	byte* getFirstFreeBlock();

	uint32_t getFlashSize();

	void eraseBlock(uint32_t address, uint32_t length);

	uint32_t getOffset(byte* address)
	{
		return address - _memoryBasePtr;
	}

	/// <summary>
	/// Write a block to flash
	/// </summary>
	/// <param name="address">Address, relative to start of flash</param>
	/// <param name="data">Pointer to data</param>
	/// <param name="dataLength">Length of data</param>
	/// <returns>True on success, false otherwise</returns>
	boolean write(uint32_t address, byte* data, uint32_t dataLength);

	/// <summary>
	/// Write a block to flash
	/// </summary>
	/// <param name="address">Address, absolute</param>
	/// <param name="data">Pointer to data</param>
	/// <param name="dataLength">Length of data</param>
	/// <returns>True on success, false otherwise</returns>
	boolean write(byte* address, byte* data, uint32_t dataLength);
};
