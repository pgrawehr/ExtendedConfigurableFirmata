// WifiCachingStream.h

#ifndef _WifiCachingStream_h
#define _WifiCachingStream_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include <WiFi.h>

/// <summary>
/// A stream to read data from a TCP connection (server side).
/// The "caching" part is not implemented yet.
/// </summary>
class WifiCachingStream : public Stream
{
private:
	WiFiServer _server;
	WiFiClient _activeClient;
	bool _hasActiveClient;
public:
	WifiCachingStream(int port) :
	_server(port), _activeClient()
	{
		_hasActiveClient = false;
	}

	void Init()
	{
		_server.begin();
	}

	bool Connect();

	int read() override;

	size_t readBytes(char* buffer, size_t length) override;

	size_t write(byte b) override;

	void maintain();

	int available() override
	{
		return _activeClient.available();
	}

	int peek() override
	{
		return _activeClient.peek();
	}

	void flush() override
	{
		// Do nothing. The implementation of WifiClient clears the _INPUT_ queue instead of the output queue!
		// _activeClient.flush();
	}
};
#endif

