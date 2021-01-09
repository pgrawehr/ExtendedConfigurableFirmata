#pragma once
#include "WProgram.h"
#include "HardwareSerial.h"

#undef INPUT
#include "WinSock2.h"

class NetworkConnection : public Serial
{
private:
	static const int DATA_BUF_SIZE = 256;
	SOCKET _listen;
	SOCKET _client;
	WSADATA _data;
	byte _dataBuf[DATA_BUF_SIZE];
	int _writeOffset;
	int _readOffset;
public:
	NetworkConnection();
	
	virtual void begin() override
	{
		begin(0);
	}

	~NetworkConnection() override;
	virtual void begin(int baudRate) override;

	virtual int read() override;

	virtual size_t write(byte b) override;

	virtual void end() override;

	virtual int available() override;

	virtual void flush() override;

private:
	void acceptNew();
};

extern class NetworkConnection NetworkSerial;
