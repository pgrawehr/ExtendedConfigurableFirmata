#pragma once

#include <ConfigurableFirmata.h>

#ifndef ESP32
// Dummy definition if not ESP
typedef int esp_partition_t;
#endif

typedef uint32_t spi_flash_mmap_handle_t;
// This class uses a special flash partition as backend storage.
class Esp32CliFlashStorage
{
private:
	const esp_partition_t* partition;
	const byte* mappedBaseAddress;
	size_t partitionSize;
	spi_flash_mmap_handle_t mapHandle;

public:
	Esp32CliFlashStorage();
	void MapFlash();
	void UnmapFlash();
	void PrintPartitions();

	~Esp32CliFlashStorage();

	byte* readAddress(uint32_t address);
	uint32_t getOffset(byte* address);

	byte* getFirstFreeBlock()
	{
		return (byte*)mappedBaseAddress;
	}

	uint32_t getFlashSize()
	{
		return partitionSize;
	}

	void eraseBlock(uint32_t address, uint32_t length);

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
	size_t getFlashPageSize();
};
