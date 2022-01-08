#pragma once

#define SIM 1
// Simulate an Arduino due. This has sizeof(int)=4, which is the same than for a 32bit windows exe
#define __SAM3X8E__ 1

// Enable the debug runtime in debug mode
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdarg.h>
#include <cstdio>
#include <string>
#include <cstdint>

typedef char __FlashStringHelper;

typedef uint8_t byte;
typedef byte boolean;

#define __attribute__(x)

// Prototypes for Arduino hardware-related functions
int digitalRead(int pin);
void digitalWrite(int pin, int value);
int analogRead(int pin);
void analogWrite(int pin, int value);
void pinMode(int pin, int mode);
void delay(int timeMs);
void delayMicroseconds(int micros);
int millis();
unsigned long micros();
byte digitalPinToBitMask(int pin);
byte digitalPinToPort(int pin);
byte* portModeRegister(int port);
byte* portOutputRegister(int port);
int digitalPinToInterrupt(int pin);
void attachInterrupt(uint8_t pin, void (*)(void), int mode);
void attachInterruptArg(uint8_t pin, void (*)(void*), void* arg, int mode);
void detachInterrupt(uint8_t pin);

void noInterrupts();
void interrupts();

/* RTOS implementation functions */
void* xSemaphoreCreateBinary();

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

#define HIGH 1
#define LOW 0

//Interrupt Modes
#define RISING    0x01
#define FALLING   0x02
#define CHANGE    0x03
#define ONLOW     0x04
#define ONHIGH    0x05
#define ONLOW_WE  0x0C
#define ONHIGH_WE 0x0D

// The program memory directive (just uses plain char* for us)
#define F(x) x
#define PROGMEM

#define B01111111 0x7F

#define A0 54
#define A1 55

/// <summary>
/// This simulated stream base class is actually a null device
/// </summary>
class Stream
{
public:
	virtual void begin();
	virtual void begin(int baudRate);
	virtual size_t write(byte b);
	virtual size_t write(const uint8_t* buf, size_t size);
	virtual int peek();
	virtual void flush();
	virtual int read();
	virtual int available();

	virtual void println(const char* output)
	{
		printf("%s\r\n", output);
	}

	virtual void println(int data)
	{
		printf("%i\r\n", data);
	}

	virtual ~Stream()
	{
	}
};

class Serial : public Stream
{
public:
	virtual void end();
};

// For String(int, int) overloads
#define DEC 10
#define HEX 16

class String
{
private:
	std::string _str;
public:
	String(char* s)
	{
		_str = std::string(s);
	}

	String(int value, int base)
	{
		char buf[100];
		_itoa_s(value, buf, 100, base);
		_str = buf;
	}

	size_t length()
	{
		return _str.length();
	}

	char charAt(int idx)
	{
		return _str.at(idx);
	}
};

// These serial lines are available on the Due
extern class Serial Serial;
extern class Serial SerialUSB;
extern class Serial Serial1;
extern class Serial Serial2;
extern class Serial Serial3;

// Include standard libs
#include "pgmspace.h"
#include "HardwareSerial.h"
#include "FSSim.h" // File system abstraction functions
