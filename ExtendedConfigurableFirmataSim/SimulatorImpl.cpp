#include "WProgram.h"
#include <utility/Boards.h>

#include "SimulatorImpl.h"
#undef INPUT
#include <Windows.h>

class Serial Serial;
class Serial SerialUSB;
class Serial Serial1;
class Serial Serial2;
class Serial Serial3;

int digitalRead(int pin)
{
	return 0;
}

void digitalWrite(int pin, int value)
{
	if (pin == VERSION_BLINK_PIN)
	{
		if (value)
		{
			printf("LED on\r\n");
		}
		else
		{
			printf("LED off\r\n");
		}
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
