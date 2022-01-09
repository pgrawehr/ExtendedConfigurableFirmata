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

void TwoWire::ReplyShort(int16_t value)
{
	_replyBuffer.push((value) & 0xFF);
	_replyBuffer.push((value >> 8) & 0xFF);
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t numBytes)
{
	_currentDevice = address;
	// BME680 Chip IDs
	if (_currentDevice == 0x76 || _currentDevice == 0x77)
	{
		// Chip ID
		if (_currentRegister == 0xD0)
		{
			_replyBuffer.push(0x61);
			return numBytes;
		}
		else if (_currentRegister == 0xE9 && numBytes == 2) // Calibration register T1
		{
			_replyBuffer.push(0xab); // 26027
			_replyBuffer.push(0x65);
		}
		else if (_currentRegister == 0x8A && numBytes == 2) // Calibration register T2
		{
			_replyBuffer.push(0xeb); // 26603
			_replyBuffer.push(0x67);
		}
		else if (_currentRegister == 0x8C && numBytes == 1) // Calibration register T3
		{
			_replyBuffer.push(0x3); // 3
		}
		else if (_currentRegister == 0x22 && numBytes == 3) // Raw temperature register
		{
			_replyBuffer.push(0x75); // 7825152
			_replyBuffer.push(0x67);
			_replyBuffer.push(0x0);
		}
		else if (_currentRegister == 0x8E && numBytes == 2) // P1
		{
			ReplyShort(-28984); // 36552, unsigned
		}
		else if (_currentRegister == 0x90 && numBytes == 2) // P2
		{
			ReplyShort(-10293);
		}
		else if (_currentRegister == 0x92 && numBytes == 1) // P3
		{
			_replyBuffer.push(88);
		}
		else if (_currentRegister == 0x94 && numBytes == 2) // P4
		{
			ReplyShort(4515);
		}
		else if (_currentRegister == 0x96 && numBytes == 2) // P5
		{
			ReplyShort(-125);
		}
		else if (_currentRegister == 0x99 && numBytes == 1) // P6
		{
			_replyBuffer.push(30);
		}
		else if (_currentRegister == 0x98 && numBytes == 1) // P7
		{
			_replyBuffer.push(33);
		}
		else if (_currentRegister == 0x9C && numBytes == 2) // P8
		{
			ReplyShort(-1511);
		}
		else if (_currentRegister == 0x9E && numBytes == 2) // P9
		{
			ReplyShort(-3062);
		}
		else if (_currentRegister == 0xA0 && numBytes == 1) // P10
		{
			_replyBuffer.push(30);
		}
		else if (_currentRegister == 0x1F && numBytes == 3) // Raw temperature register
		{
			_replyBuffer.push(0x67); // 6753536
			_replyBuffer.push(0x0D);
			_replyBuffer.push(0x0);
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
	return requestFrom((byte)address, (byte)numBytes);
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





