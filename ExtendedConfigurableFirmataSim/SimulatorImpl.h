#pragma once
#include "WProgram.h"
#include "HardwareSerial.h"

#undef INPUT
#include "WinSock2.h"

class NetworkConnection : public Serial
{
private:
	SOCKET _socket;
public:
	virtual void begin() override
	{
		begin(0);
	}

	virtual void begin(int baudRate) override;

	virtual int read() override;

	virtual void write(byte b) override;

	virtual void end() override;
};

