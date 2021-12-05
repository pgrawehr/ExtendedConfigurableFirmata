#pragma once
#include "WProgram.h"
#include "HardwareSerial.h"
#include <queue>

#undef INPUT
#include "WinSock2.h"

class NetworkConnection : public Serial
{
private:
	static const int DATA_BUF_SIZE = 256;
	SOCKET _listen;
	SOCKET _client;
	WSADATA _data;
	std::queue<byte> _queue;
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
