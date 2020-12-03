// Simulator implementation for Wire.h (Arduino i2c library)
#include "WProgram.h"
#include <utility/Boards.h>

#include "SimulatorImpl.h"
#undef INPUT
#include <Windows.h>

#include "Wire.h"

// The global instance
TwoWire Wire;

TwoWire::TwoWire()
{
}


void TwoWire::begin()
{
}

void TwoWire::beginTransmission(uint8_t address)
{
}

uint8_t TwoWire::endTransmission()
{
	return 0;
}

uint8_t TwoWire::endTransmission(uint8_t stopTx)
{
	return 0;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t numBytes)
{
	return 0;
}

size_t TwoWire::write(uint8_t)
{
	return 1;
}

int TwoWire::available()
{
	return 0;
}

int TwoWire::read()
{
	return 0;
}

int TwoWire::peek()
{
	return 0;
}

void TwoWire::end()
{
}

void TwoWire::flush()
{
}

size_t TwoWire::write(const uint8_t* data, size_t length)
{
	return length;
}





