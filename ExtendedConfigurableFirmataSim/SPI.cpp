// Simulator implementation for SPI.h (Arduino SPI library)
#include "WProgram.h"
#include <utility/Boards.h>

#include "SimulatorImpl.h"
#undef INPUT
#include <Windows.h>

#include "SPI.h"

SPIClass SPI;

void SPIClass::transfer(byte _pin, void* _buf, size_t _count, SPITransferMode _mode)
{
	byte* buf = (byte*)_buf;
	// The reply is all zeroes now
	for (size_t i = 0; i < _count; i++)
	{
		buf[i] = 0;
	}
}

void SPIClass::begin()
{
}

void SPIClass::end()
{
}

void SPIClass::beginTransaction(uint8_t pin, SPISettings settings)
{
}

void SPIClass::endTransaction()
{
}
