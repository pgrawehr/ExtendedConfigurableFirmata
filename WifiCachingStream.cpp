// 
// 
// 

#include <ConfigurableFirmata.h>
#include "WifiCachingStream.h"
#ifdef ESP32
void WifiCachingStream::Init()
{
	// Also initializes other background processes
	// ESP_ERROR_CHECK(esp_event_loop_create_default());
	_server.begin();
}

bool WifiCachingStream::Connect()
{
	if (_activeClient.connected())
	{
		return true;
	}

	_activeClient.stop();
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
		// Low-power mode significantly increases round-trip time, but when nobody
		// is connected, that's ok.
		WiFi.setSleep(true);
		Firmata.resetParser(); // clear any partial message from the parser when the connection is dropped.
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
	return _activeClient.readBytes(buffer, length);
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
	auto newClient = _server.accept();
	if (newClient.connected() && newClient.fd() != _activeClient.fd())
	{
		// We have a new client. For now, we drop the old an accept the new, because the old one might be a dead connection.
		// Allowing multiple connections would also be an option, but that would need to be considered separately
		Serial.println("New incoming connection - dropping existing");
		_activeClient.stop();
		_activeClient = newClient;
	}
	yield();
}

#endif
