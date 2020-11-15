#pragma once
#include "WProgram.h"
#include "HardwareSerial.h"

#undef INPUT
#include "WinSock2.h"

class NetworkConnection : public Serial
{
private:
	SOCKET _listen;
	SOCKET _client;
	WSADATA _data;
public:
	NetworkConnection();
	
	virtual void begin() override
	{
		begin(0);
	}

	~NetworkConnection() override;
	virtual void begin(int baudRate) override;

	virtual int read() override;

	virtual void write(byte b) override;

	virtual void end() override;

	virtual int available() override;

	virtual void flush() override;

private:
	void acceptNew();
};

extern class NetworkConnection NetworkSerial;
