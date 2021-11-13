// 
// 
// 

#include <ConfigurableFirmata.h>
#include "WifiCachingStream.h"


bool WifiCachingStream::Connect()
{
	if (_activeClient)
	{
		return true;
	}
	
	_activeClient = _server.accept();

	if (_activeClient)
	{
		Serial.println("New client connected");
		WiFi.setSleep(false);
		_hasActiveClient = true;
		return true;
	}

	if (_hasActiveClient)
	{
		_hasActiveClient = false;
		Serial.println("Client disconnected - entering WiFi low-power mode");
		WiFi.setSleep(true);
	}
	return false;
}

int WifiCachingStream::read()
{
	if (!Connect() || _activeClient.available() <= 0)
	{
		return -1;
	}
	return _activeClient.read();
}

size_t WifiCachingStream::readBytes(char* buffer, size_t length)
{
	if (!Connect() || _activeClient.available() <= 0)
	{
		return -1;
	}
	return _activeClient.read();
}

size_t WifiCachingStream::write(byte b)
{
	if (!Connect())
	{
		return 0;
	}

	return _activeClient.write(b);
}

void WifiCachingStream::maintain()
{
	Connect();
	yield();
}
