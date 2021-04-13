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
	_currentDevice = address;
	// BME680 Chip ID
	if (_currentDevice == 0x76)
	{
		// Chip ID
		if (_currentRegister == 0xD0)
		{
			_replyBuffer.push(0x61);
			return numBytes;
		}
		else
		{
			for (int i = 0; i < numBytes; i++)
			{
				_replyBuffer.push(0x0);
			}
			return numBytes;
		}
	}
	return 0;
}

uint8_t TwoWire::requestFrom(int address, int numBytes)
{
	_currentDevice = address;
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
	return _replyBuffer.size();
}

int TwoWire::read()
{
	int ret = _replyBuffer.front();
	_replyBuffer.pop();
	return ret;
}

int TwoWire::peek()
{
	if (_replyBuffer.empty())
	{
		return 0;
	}
	return _replyBuffer.front();
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





