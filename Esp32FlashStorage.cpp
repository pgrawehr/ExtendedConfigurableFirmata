
#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "Esp32FlashStorage.h"
#ifdef ESP32
#include "esp_partition.h"

const char* FLASH_TAG = "[FLASH]";
// This flash storage driver uses the partition named "ilcode" on the ESP32 embedded SPI flash

Esp32CliFlashStorage::Esp32CliFlashStorage()
{
	partition = nullptr;
	mappedBaseAddress = nullptr;
	partitionSize = 0;
	mapHandle = 0;

	partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ilcode");
	if (partition == nullptr)
	{
		ESP_LOGE(FLASH_TAG, "FATAL: Could not locate data partition.");
		return;
	}

	partitionSize = partition->size;
	ESP_LOGI(FLASH_TAG, "Data partition found.");

	// TEST CODE BELOW
	eraseBlock(0, partitionSize);
	int a = 0xabcdef01;
	write((uint32_t)0, (byte*) &a, (uint32_t)4);
	MapFlash();
	int b = *(int32_t*)mappedBaseAddress;
	ESP_LOGI(FLASH_TAG, "Flash test: Input %d, output %d", a, b);
	// PrintPartitions();
}

void Esp32CliFlashStorage::MapFlash()
{
	if (mapHandle != 0)
	{
		return; // already mapped
	}
	const void* map_ptr;
	spi_flash_mmap_handle_t map_handle;

	// Map the partition to data memory
	if (esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &map_ptr, &map_handle) != 0)
	{
		ESP_LOGE(FLASH_TAG, "FATAL: Could not map data partition to RAM");
		return;
	}

	mappedBaseAddress = (byte*)map_ptr;
	mapHandle = map_handle;
	ESP_LOGI(FLASH_TAG, "ILCode partition mapped to 0x%x, size %d", mappedBaseAddress, partitionSize);
}

void Esp32CliFlashStorage::UnmapFlash()
{
	if (mapHandle != 0)
	{
		// We keep mappedBaseAddress, since we need it for the translation, assuming that another map will give us the same base address.
		// Otherwise, our logic wouldn't work anyway.
		esp_partition_munmap(mapHandle);
		mapHandle = 0;
	}
}



Esp32CliFlashStorage::~Esp32CliFlashStorage()
{
	UnmapFlash();
}

void Esp32CliFlashStorage::PrintPartitions()
{
	esp_partition_iterator_t iter = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
	ESP_LOGI(FLASH_TAG, "Name, type, subtype, offset, length");
	while (iter != nullptr)
	{
		const esp_partition_t* partition = esp_partition_get(iter);
		ESP_LOGI(FLASH_TAG, "%s, app, %d, 0x%x, 0x%x (%d)", partition->label, partition->subtype, partition->address, partition->size, partition->size);
		iter = esp_partition_next(iter);
	}

	esp_partition_iterator_release(iter);
	iter = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
	while (iter != nullptr)
	{
		const esp_partition_t* partition = esp_partition_get(iter);
		ESP_LOGI(FLASH_TAG, "%s, data, %d, 0x%x, 0x%x (%d)", partition->label, partition->subtype, partition->address, partition->size, partition->size);
		iter = esp_partition_next(iter);
	}

	esp_partition_iterator_release(iter);
}

byte* Esp32CliFlashStorage::readAddress(uint32_t address)
{
	if (mappedBaseAddress == nullptr)
	{
		ESP_LOGE(FLASH_TAG, "Attempting to read from unmapped memory");
		return nullptr;
	}
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
	Firmata.sendStringf(F("Writing flash at 0x%x, length %lu"), address, dataLength);
	esp_err_t errNo = esp_partition_write_raw(partition, address, data, dataLength);
	if (errNo != 0)
	{
		Firmata.sendStringf(F("Error: Could not write flash: %d"), errNo);
		throw stdSimple::OutOfMemoryException("Esp32FlashStorage: Error writing flash");
	}

	/*byte* physicalAddress = readAddress(address);
	int result = memcmp(data, physicalAddress, dataLength);
	if (result != 0)
	{
		Firmata.sendStringf(F("Error: Data at address 0x%x not written correctly"), physicalAddress);
		return false;
	}*/
	
	return true;
}

void Esp32CliFlashStorage::eraseBlock(uint32_t address, uint32_t length)
{
	Firmata.sendStringf(F("Erasing data partition from 0x%x, length %lu (erase allignment %ld)"), address, length, partition->erase_size);
	esp_err_t errNo = esp_partition_erase_range(partition, address, length);
	if (errNo != 0)
	{
		Firmata.sendStringf(F("Error: Could not erase flash: %d"), errNo);
		throw stdSimple::OutOfMemoryException("Esp32FlashStorage: Error erasing flash");
	}
}

#endif
