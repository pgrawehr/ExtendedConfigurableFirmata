// FlashMemoryManager.h

#ifndef _FLASHMEMORYMANAGER_h
#define _FLASHMEMORYMANAGER_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

struct FlashMemoryHeader;

class FlashMemoryManager
{
private:
	byte* _endOfHeap;
	byte* _startOfHeap;
	byte* _flashEnd;
	FlashMemoryHeader* _header;
	bool _headerClear; // This is set to true to indicate the header is invalid, even if it's contents would still be ok
	bool _flashClear; // This is true if the flash memory is known to be cleared
public:
	FlashMemoryManager();

	void Init(void*& classes, void*& methods, void*& constants, void*& stringHeap, int*& specialTokenList, void*& clauses, int&
	          startupToken, int& startupFlags, uint32_t& staticVectorMemorySize);
	/// <summary>
	/// Allocate memory in flash.
	/// Note that: a) The memory cannot be freed so far, except clearing the whole block. b) The returned address cannot be used directly as a target for
	/// a memory write operation. It must be used as input parameter for CopyToFlash (it can be used if relocation elsewhere is required, though, or later for reading)
	/// </summary>
	/// <param name="bytes">Number of bytes to allocate</param>
	/// <returns>A memory address</returns>
	void* FlashAlloc(size_t bytes);

	void CopyToFlash(void* src, void* flashTarget, size_t length, const char* usage);
	void WriteHeader(int dataVersion, int hashCode, void* classesPtr, void* methodsPtr, void* constantsPtr, void* stringHeapPtr, int*
	                 specialTokenList, void* clauses, int startupToken, int startupFlags, int staticVectorMemorySize);

	/// <summary>
	/// Marks the flash as empty. It does not write anything yet, so if this is called without a subsequent CopyToFlash or WriteHeader, the memory will still be there after bootup
	/// </summary>
	void Clear();

	bool ContainsMatchingData(int dataVersion, int hashCode);

	long TotalFlashMemory() const;

	long UsedFlashMemory();

	/// <summary>
	/// Check that the flash content is consistent and matches the current firmware
	/// </summary>
	/// <returns>True on success, false otherwise</returns>
	bool ValidateFlashContents() const;

private:

	/// <summary>
	/// Read the header from flash and initialize the addresses
	/// </summary>
	/// <returns> True if the header contains a valid signature, false otherwise</returns>
	bool InitHeader();
};

#endif
