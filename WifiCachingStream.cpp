// 
// 
// 

#include <ConfigurableFirmata.h>
#include "WifiCachingStream.h"

#include <sys/poll.h>
#ifdef ESP32
#include "ftp.h"

void WifiCachingStream::Init()
{
	// Also initializes other background processes
	// ESP_ERROR_CHECK(esp_event_loop_create_default());
	if (!ftp_create_listening_socket(&_sd, _port, 1))
	{
		Firmata.sendStringf(F("Error opening listening socket."));
	}
}

bool WifiCachingStream::Connect()
{
	if (_connection_sd >= 0)
	{
		return true;
	}

	if (_sd < 0)
	{
		return false;
	}

	auto result = ftp_wait_for_connection(_sd, &_connection_sd, nullptr, true);
	if (result != E_FTP_RESULT_OK)
	{
		_connection_sd = -1;
		return false;
	}

	if (_connection_sd >= 0)
	{
		Serial.println("New client connected");
		WiFi.setSleep(false);
		return true;
	}
	else
	{
		Serial.println("Client disconnected - entering WiFi low-power mode");
		// Low-power mode significantly increases round-trip time, but when nobody
		// is connected, that's ok.
		// WiFi.setSleep(true);
		Firmata.resetParser(); // clear any partial message from the parser when the connection is dropped.

		return false;
	}
}

int WifiCachingStream::read()
{
	if (_connection_sd < 0)
	{
		return -1;
	}

	char data;
	int received = 0;
	auto result = ftp_recv_non_blocking(_connection_sd, &data, 1, &received);
	if (received == 1)
	{
		return data;
	}
	if (result == E_FTP_RESULT_FAILED)
	{
		ftp_close_socket(&_connection_sd);
		Firmata.sendStringf(F("Connection dropped"));
	}

	return -1;
}

int WifiCachingStream::available()
{
	if (_connection_sd < 0)
	{
		return -1;
	}

	return ftp_poll(&_connection_sd);
}

int WifiCachingStream::peek()
{
	return -1;
}

void WifiCachingStream::flush()
{
	// probably nothing to do
}



size_t WifiCachingStream::readBytes(char* buffer, size_t length)
{
	int received = 0;
	auto result = ftp_recv_non_blocking(_connection_sd, buffer, 1, &received);
	if (received == 1)
	{
		return received;
	}
	if (result == E_FTP_RESULT_FAILED)
	{
		ftp_close_socket(&_connection_sd);
		Firmata.sendStringf(F("Connection dropped"));
	}

	return -1;
}

size_t WifiCachingStream::write(byte b)
{
	if (b == START_SYSEX)
	{
		_inSysex = true;
	}
	else if (b == END_SYSEX)
	{
		_inSysex = false;
	}

	_sendBuffer[_sendBufferIndex] = b;
	_sendBufferIndex++;
	// Send when the buffer is full or we're not in a sysex message or at the end of it.
	if (_sendBufferIndex >= SendBufferSize || _inSysex == false)
	{
		int ret = ftp_send(_connection_sd, _sendBuffer, _sendBufferIndex);
		_sendBufferIndex = 0;
		return ret >= 1;
	}
	return 1;
}

size_t WifiCachingStream::write(const uint8_t* buffer, size_t size)
{
	return ftp_send(_connection_sd, buffer, size);
}


void WifiCachingStream::maintain()
{
	Connect();
}

#endif
