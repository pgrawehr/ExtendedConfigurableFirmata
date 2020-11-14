#include "WProgram.h"
#include <utility/Boards.h>

#include "SimulatorImpl.h"
#undef INPUT
#include <Windows.h>
#include <Ws2tcpip.h>

class Serial Serial;
class Serial SerialUSB;
class Serial Serial1;
class Serial Serial2;
class Serial Serial3;
class NetworkConnection NetworkSerial;

int digitalRead(int pin)
{
	return 0;
}

void digitalWrite(int pin, int value)
{
	if (value)
	{
		wprintf(L"LED %d on\r\n", pin);
	}
	else
	{
		wprintf(L"LED %d off\r\n", pin);
	}
}

int analogRead(int pin)
{
	return 500;
}

void analogWrite(int pin, int value)
{
	
}
void pinMode(int pin, int mode)
{
}
void delay(int timeMs)
{
	Sleep(timeMs);
}

void delayMicroseconds(int micros)
{
	// We're not expecting that the simulation is anywhere time-criticial (or has similar timings than the arduino, anyway)
	return Sleep(micros / 1000);
}

int millis()
{
	return GetTickCount();
}

int micros()
{
	return GetTickCount();
}

void Stream::begin()
{
}

void Stream::begin(int baudRate)
{
}

void Stream::write(byte b)
{
	
}

void Stream::flush()
{
	
}

int Stream::read()
{
	return -1;
}

int Stream::available()
{
	return 0;
}

void Serial::end()
{
}

void NetworkConnection::begin(int baudRate)
{
	int iResult = WSAStartup(MAKEWORD(2, 2), &_data);
	if (iResult != NO_ERROR) {
		wprintf(L"Error at WSAStartup()\n");
		return;
	}
	_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_listen == INVALID_SOCKET)
	{
		wprintf(L"socket function failed with error = %d\n", WSAGetLastError());
	}
	else 
	{
		wprintf(L"socket function succeeded\n");
	}

	// The socket address to be passed to bind
	sockaddr_in service;

	service.sin_family = AF_INET;
	InetPton(AF_INET, L"127.0.0.1", &service.sin_addr.s_addr);
	service.sin_port = htons(27015);

	iResult = bind(_listen, (SOCKADDR*)&service, sizeof(service));
	if (iResult == SOCKET_ERROR) {
		wprintf(L"bind failed with error %u\n", WSAGetLastError());
		closesocket(_listen);
		WSACleanup();
		return;
	}
	else
		wprintf(L"bind returned success\n");
	//----------------------
	// Listen for incoming connection requests 
	// on the created socket
	if (listen(_listen, SOMAXCONN) == SOCKET_ERROR)
		wprintf(L"listen function failed with error: %d\n", WSAGetLastError());

	wprintf(L"Listening on socket...\n");

	_client = INVALID_SOCKET;

	// Accept a client socket
	_client = accept(_listen, NULL, NULL);
	if (_client == INVALID_SOCKET) 
	{
		printf("accept failed: %d\n", WSAGetLastError());
		closesocket(_listen);
		WSACleanup();
		return;
	}
}

void NetworkConnection::end()
{
	closesocket(_listen);
	closesocket(_client);
	_listen = INVALID_SOCKET;
	WSACleanup();
}

int NetworkConnection::read()
{
	// Receive single bytes only
	char buf[1];
	int ret = recv(_client, buf, 1, 0);
	
	if (ret > 0)
	{
		return buf[0] & 0xFF; // Otherwise, this is sign-extended here
	}

	return -1;
}

void NetworkConnection::write(byte b)
{
	char buf[1];
	buf[0] = (char)b;
	send(_client, buf, 1, 0);
}

int NetworkConnection::available()
{
	char buf[1];
	int ret = recv(_client, buf, 1, MSG_PEEK);

	if (ret > 0)
	{
		return ret;
	}

	return 0;
}

void NetworkConnection::flush()
{
}




