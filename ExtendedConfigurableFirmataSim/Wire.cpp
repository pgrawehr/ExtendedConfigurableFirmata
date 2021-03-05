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
	_currentDevice = 0;
	_currentRegister = 0;
	_bytesInCurrentTransmission = 0;
}


void TwoWire::begin()
{
}

void TwoWire::beginTransmission(uint8_t address)
{
	_currentDevice = address;
	_bytesInCurrentTransmission = 0;
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

uint8_t TwoWire::requestFrom(int address, int numBytes)
{
	_currentRegister = address;
	// TODO: For improved simulation, Simulate reply from a DS1307 RTC here
	// if (_currentRegister == 0 && _currentDevice == 0x68)
	// {
	// }

	return (byte)numBytes;
}


size_t TwoWire::write(uint8_t data)
{
	if (_bytesInCurrentTransmission == 0)
	{
		_currentRegister = data;
	}

	
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





