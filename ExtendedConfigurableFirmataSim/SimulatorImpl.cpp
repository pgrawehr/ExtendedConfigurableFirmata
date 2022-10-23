#include "WProgram.h"
#include <utility/Boards.h>

#include "SimulatorImpl.h"
#include "../SimFlashStorage.h"

#undef INPUT
#include <Windows.h>
#include <WS2tcpip.h>

class Serial Serial;
class Serial SerialUSB;
class Serial Serial1;
class Serial Serial2;
class Serial Serial3;
class NetworkConnection NetworkSerial;

// The mode of each pin
static byte pinModes[TOTAL_PINS] = {0};

// The value of each pin
static byte pinValues[TOTAL_PINS] = {0};
// Used for temporary return values
static byte temp;

int digitalRead(int pin)
{
	if (pin < TOTAL_PINS)
	{
		return pinValues[pin];
	}
	return 0;
}

void ToggleSimulatedPin(int pin)
{
	if (pin < TOTAL_PINS && pin >= 0)
	{
		pinValues[pin] = pinValues[pin] ? 0 : 1;
	}
}

void UpdateDisplay()
{
	printf("\r");
	for (int i = 0; i < TOTAL_PINS; i++)
	{
		if (pinModes[i] == PIN_MODE_INPUT)
		{
			if (pinValues[i])
			{
				printf("1");
			}
			else
			{
				printf("0");
			}
		}
		else if (pinModes[i] == PIN_MODE_OUTPUT)
		{
			if (pinValues[i])
			{
				printf("H");
			}
			else
			{
				printf("L");
			}
		}
		else if (pinModes[i] == PIN_MODE_I2C)
		{
			if (pinValues[i])
			{
				printf("B");
			}
			else
			{
				printf("B");
			}
		}
		else
		{
			printf("_");
		}
	}
}

void digitalWrite(int pin, int value)
{
	pinValues[pin] = value;
	UpdateDisplay();
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
	if (pin < TOTAL_PINS)
	{
		pinModes[pin] = (byte)mode;
		UpdateDisplay();
	}
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

CRITICAL_SECTION cs;

void noInterrupts()
{
	EnterCriticalSection(&cs);
}

void interrupts()
{
	LeaveCriticalSection(&cs);
}

int digitalPinToInterrupt(int pin)
{
	return pin;
}

void attachInterrupt(uint8_t pin, void (*)(void), int mode)
{
	// Does just nothing in simulation for now
}
void attachInterruptArg(uint8_t pin, void (*)(void*), void* arg, int mode)
{
	
}
void detachInterrupt(uint8_t pin)
{
	
}

unsigned long micros()
{
	const int ONEMILLION = 1000000;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	LARGE_INTEGER current;
	QueryPerformanceCounter(&current);
	LONGLONG ret;
	if (freq.QuadPart >= ONEMILLION)
	{
		LONGLONG divisor = freq.QuadPart / ONEMILLION;
		ret = (current.QuadPart / divisor);
	}
	else
	{
		LONGLONG multiplier = ONEMILLION / freq.QuadPart;
		ret = (current.QuadPart * multiplier);
	}
	return (unsigned long)ret;
}

void Stream::begin()
{
}

void Stream::begin(int baudRate)
{
}

size_t Stream::write(byte b)
{
	return 1;
}

void Stream::flush()
{
	
}

int Stream::read()
{
	return -1;
}

int Stream::readBytes(uint8_t* buf, size_t length)
{
	if (length == 0)
	{
		return 0;
	}
	int data = read();
	if (data != -1)
	{
		buf[0] = (byte)data;
		return 1;
	}

	return 0;
}


int Stream::available()
{
	return 0;
}

size_t Stream::write(const uint8_t* buf, size_t size)
{
	return size;
}

int Stream::peek()
{
	return -1;
}



void Serial::end()
{
}

NetworkConnection::NetworkConnection()
	: _data()
{
	_listen = INVALID_SOCKET;
	_client = INVALID_SOCKET;
	InitializeCriticalSection(&cs);
}

NetworkConnection::~NetworkConnection()
{
	WSACleanup();
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
	service.sin_port = htons(27016);

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
	{
		wprintf(L"listen function failed with error: %d\n", WSAGetLastError());
		return;
	}
	
	u_long iMode = 1;
	if (ioctlsocket(_listen, FIONBIO, &iMode) == SOCKET_ERROR)
	{
		wprintf(L"ioctlsocket function failed with error: %d\n", WSAGetLastError());
		return;
	}

	_client = INVALID_SOCKET;

	wprintf(L"Waiting for client...\n");
	acceptNew();

}

void NetworkConnection::acceptNew()
{
	_client = INVALID_SOCKET;
	// Accept a client socket
	_client = accept(_listen, NULL, NULL);
	if (_client == INVALID_SOCKET)
	{
		int error = WSAGetLastError();
		if (error == WSAEWOULDBLOCK)
		{
			Sleep(50);
			return;
		}
		
		printf("accept failed: %d\n", error);
		return;
	}

	//-------------------------
	// Set the socket I/O mode: In this case FIONBIO
	// enables or disables the blocking mode for the 
	// socket based on the numerical value of iMode.
	// If iMode = 0, blocking is enabled; 
	// If iMode != 0, non-blocking mode is enabled.

	u_long iMode = 1;
	int ret  = ioctlsocket(_client, FIONBIO, &iMode);
	if (ret == SOCKET_ERROR)
	{
		auto error = WSAGetLastError();
		wprintf(L"Socket error when setting to non-blocking mode: %d\n", error);
	}

	while (!_queue.empty())
	{
		_queue.pop();
	}
}

void NetworkConnection::end()
{
	closesocket(_listen);
	closesocket(_client);
	_listen = INVALID_SOCKET;
	WSACleanup();
}

int NetworkConnection::peek()
{
	return read(false);
}

int NetworkConnection::read()
{
	return read(true);
}


int NetworkConnection::read(bool doPop)
{
	if (!_queue.empty())
	{
		int valueToReturn = _queue.front() & 0xFF; // Otherwise, this is sign-extended here
		if (doPop)
		{
			_queue.pop();
		}
		return valueToReturn;
	}
	
	char buf[DATA_BUF_SIZE];
	char* bufStart = buf;
	int ret = recv(_client, buf, DATA_BUF_SIZE, 0);
	
	if (ret > 0)
	{
		while (ret > 0)
		{
			_queue.push(*bufStart);
			bufStart++;
			ret--;
		}
		return read(); // Exit trough the case above
	}

	if (ret < 0)
	{
		// There was an error
		int error = WSAGetLastError();
		if (error == WSAEWOULDBLOCK)
		{
			return -1; // No data available
		}
	}

	// If ret is 0, this always means the connection has been closed, and not that no data is available
	wprintf(L"Connection closed - waiting for new client...\n");
	closesocket(_client);
	_client = INVALID_SOCKET;
	return -1;
}

size_t NetworkConnection::write(byte b)
{
	char buf[1];
	buf[0] = (char)b;
	send(_client, buf, 1, 0);
	return 1;
}

size_t NetworkConnection::write(const uint8_t* data, size_t size)
{
	send(_client, (char*)data, (int)size, 0);
	return size;
}


int NetworkConnection::available()
{
	if (_client == INVALID_SOCKET)
	{
		acceptNew();
		return 0;
	}

	if (_queue.size() > 0)
	{
		return _queue.size();
	}
	
	char buf[1];
	int ret = recv(_client, buf, 1, MSG_PEEK);

	if (ret == SOCKET_ERROR)
	{
		auto error = WSAGetLastError();
		if (error == WSAECONNRESET || error == WSANOTINITIALISED || error == WSAECONNABORTED)
		{
			wprintf(L"Connection closed - waiting for new client...\n");

			closesocket(_client);
			_client = INVALID_SOCKET;
			acceptNew();
		}

		if (error != WSAEWOULDBLOCK)
		{
			Sleep(10);
		}
		return 0;
	}

	if (ret == 0)
	{
		// No bytes read. The socket is closed
		closesocket(_client);
		_client = INVALID_SOCKET;
		acceptNew();
	}

	return 1;
}

void NetworkConnection::flush()
{
}

extern VirtualFlashMemory* storage;
void shutdown()
{
	// Reset before shutdown, especially to clear the runtime memory, otherwise the
	// list of memory leaks will be endless
	Firmata.parse(SYSTEM_RESET); 
	delete storage;
	storage = nullptr;
}

void* xSemaphoreCreateBinary()
{
	auto handle = CreateEventEx(NULL, NULL, 0, STANDARD_RIGHTS_ALL);
	return handle;
}

