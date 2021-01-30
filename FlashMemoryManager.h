﻿// FlashMemoryManager.h

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
public:
	FlashMemoryManager();

	/// <summary>
	/// Allocate memory in flash.
	/// Note that: a) The memory cannot be freed so far, except clearing the whole block. b) The returned address cannot be used directly as a target for
	/// a memory write operation. It must be used as input parameter for CopyToFlash (it can be used if relocation elsewhere is required, though, or later for reading)
	/// </summary>
	/// <param name="bytes">Number of bytes to allocate</param>
	/// <returns>A memory address</returns>
	void* FlashAlloc(size_t bytes);

	void CopyToFlash(void* src, void* flashTarget, size_t length);
	void WriteHeader(int dataVersion, int hashCode);

	void Clear();

	bool ContainsMatchingData(int dataVersion, int hashCode);

private:
	void Init();
};

extern FlashMemoryManager FlashManager;

#endif

