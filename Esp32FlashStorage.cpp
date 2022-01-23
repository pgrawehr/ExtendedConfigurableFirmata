
#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "Esp32FlashStorage.h"
#ifdef ESP32
#include "esp_partition.h"

// This flash storage driver uses the partition named "ilcode" on the ESP32 embedded SPI flash

Esp32CliFlashStorage::Esp32CliFlashStorage()
{
	partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ilcode");
	if (partition == nullptr)
	{
		throw stdSimple::ExecutionEngineException("FATAL: Could not locate data partition.");
	}

	const void *map_ptr;
    spi_flash_mmap_handle_t map_handle;

    // Map the partition to data memory
    if (esp_partition_mmap(partition, 0, partition->size, SPI_FLASH_MMAP_DATA, &map_ptr, &map_handle) != 0)
    {
		throw stdSimple::ExecutionEngineException("FATAL: Could not map data partition to RAM");
    }

	mappedBaseAddress = (byte*)map_ptr;
	mapHandle = map_handle;
	partitionSize = partition->size;
	PrintPartitions();
}

Esp32CliFlashStorage::~Esp32CliFlashStorage()
{
	mappedBaseAddress = nullptr;
	spi_flash_munmap(mapHandle);
	mapHandle = 0;
}

void Esp32CliFlashStorage::PrintPartitions()
{
	esp_partition_iterator_t iter = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
	Firmata.sendString(F("Name, type, subtype, offset, length"));
	while (iter != nullptr)
	{
		const esp_partition_t* partition = esp_partition_get(iter);
		Firmata.sendStringf(F("%s, app, %d, 0x%x, 0x%x (%d)"), partition->label, partition->subtype, partition->address, partition->size, partition->size);
		iter = esp_partition_next(iter);
	}

	esp_partition_iterator_release(iter);
	iter = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
	while (iter != nullptr)
	{
		const esp_partition_t* partition = esp_partition_get(iter);
		Firmata.sendStringf(F("%s, data, %d, 0x%x, 0x%x (%d)"), partition->label, partition->subtype, partition->address, partition->size, partition->size);
		iter = esp_partition_next(iter);
	}

	esp_partition_iterator_release(iter);

	Firmata.sendStringf(F("Data partition of size %d mapped to memory address 0x%x"), partition->size, mappedBaseAddress);
}

byte* Esp32CliFlashStorage::readAddress(uint32_t address)
{
	return const_cast<byte*>(mappedBaseAddress + address);
}

uint32_t Esp32CliFlashStorage::getOffset(byte* address)
{
	return address - mappedBaseAddress;
}

boolean Esp32CliFlashStorage::write(byte* address, byte* data, uint32_t dataLength)
{
	uint32_t offset = getOffset(address);
	return write(offset, data, dataLength);
}

boolean Esp32CliFlashStorage::write(uint32_t address, byte* data, uint32_t dataLength)
{
	esp_err_t errNo = esp_partition_write(partition, address, data, dataLength);
	if (errNo != 0)
	{
		Firmata.sendStringf(F("Error: Could not write flash: %d"), errNo);
		throw stdSimple::OutOfMemoryException("Esp32FlashStorage: Error writing flash");
	}

	byte* physicalAddress = readAddress(address);
	int result = memcmp(data, physicalAddress, dataLength);
	if (result != 0)
	{
		Firmata.sendStringf(F("Error: Data at address 0x%x not written correctly"), physicalAddress);
		return false;
	}
	
	return true;
}

void Esp32CliFlashStorage::eraseBlock(uint32_t address, uint32_t length)
{
	esp_err_t errNo = esp_partition_erase_range(partition, address, length);
	if (errNo != 0)
	{
		Firmata.sendStringf(F("Error: Could not erase flash: %d"), errNo);
		throw stdSimple::OutOfMemoryException("Esp32FlashStorage: Error erasing flash");
	}
}

#endif
