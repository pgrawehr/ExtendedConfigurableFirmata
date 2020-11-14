#pragma once

#define SIM 1
// Simulate an Arduino due. This has sizeof(int)=4, which is the same than for a 32bit windows exe
#define __SAM3X8E__ 1

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
int micros();

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

#define HIGH 1
#define LOW 0

// The program memory directive (just uses plain char* for us)
#define F(x) x
#define PROGMEM

#define B01111111 0x7F

class Stream
{
public:
	virtual void begin();
	virtual void begin(int baudRate);
	virtual void write(byte b);
	virtual void flush();
	virtual int read();
	virtual int available();

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
