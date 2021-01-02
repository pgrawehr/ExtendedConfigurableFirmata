/*
 * CommonFirmataFeatures.ino generated by FirmataBuilder
 * Sun Mar 29 2020 15:10:48 GMT-0400 (EDT)
 */

#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "SelfTest.h"
#include "FreeMemory.h"
// Use these defines to easily enable or disable certain modules

/* Note: Currently no client support by dotnet/iot for these, so they're disabled by default */
/* Enabling all modules on the smaller Arduino boards (such as the UNO or the Nano) won't work anyway, as there is both
 * not enough flash as well as not enough RAM. 
 */

// #define ENABLE_ONE_WIRE
// #define ENABLE_SERVO 
// #define ENABLE_ACCELSTEPPER
#define ENABLE_BASIC_SCHEDULER
#define ENABLE_SERIAL

/* Native reading of DHTXX sensors. Reading a DHT11 directly using GPIO methods from a remote PC will not work, because of the very tight timing requirements of these sensors*/
#ifndef SIM
#define ENABLE_DHT
#endif
#define ENABLE_I2C
#define ENABLE_IL_EXECUTOR
#define ENABLE_SPI
#define ENABLE_ANALOG
#define ENABLE_DIGITAL

#ifdef SIM
#include "SimulatorImpl.h"
#endif

#ifdef ENABLE_DIGITAL
#include <DigitalInputFirmata.h>
DigitalInputFirmata digitalInput;

#include <DigitalOutputFirmata.h>
DigitalOutputFirmata digitalOutput;
#endif

#ifdef ENABLE_ANALOG
#include <AnalogInputFirmata.h>
AnalogInputFirmata analogInput;

#include <AnalogOutputFirmata.h>
AnalogOutputFirmata analogOutput;
#include <AnalogWrite.h>
#endif

#ifdef ENABLE_I2C
#include <Wire.h>
#include <I2CFirmata.h>
I2CFirmata i2c;
#endif

#ifdef ENABLE_ONE_WIRE
#include <OneWireFirmata.h>
OneWireFirmata oneWire;
#endif

#ifdef ENABLE_SERIAL
#include <SerialFirmata.h>
SerialFirmata serial;
#endif

#include <FirmataExt.h>
FirmataExt firmataExt;

#ifdef ENABLE_SPI
#include <SpiFirmata.h>
SpiFirmata spi;
#endif

#ifdef ENABLE_SERVO
#include <Servo.h>
#include <ServoFirmata.h>
ServoFirmata servo;
#endif

#include <FirmataReporting.h>
FirmataReporting reporting;

#ifdef ENABLE_ACCELSTEPPER
#include <AccelStepperFirmata.h>
AccelStepperFirmata accelStepper;
#endif

#ifdef ENABLE_DHT
#include <DhtFirmata.h>
DhtFirmata dhtFirmata;
#endif

#ifdef ENABLE_BASIC_SCHEDULER
// The scheduler allows to store scripts on the board, however this requires a kind of compiler on the client side.
// When running dotnet/iot on the client side, prefer using the FirmataIlExecutor module instead
#include <FirmataScheduler.h>
FirmataScheduler scheduler;
#endif

#ifdef ENABLE_IL_EXECUTOR
#include "FirmataIlExecutor.h"
FirmataIlExecutor ilExecutor;
#include "FirmataStatusLed.h"
FirmataStatusLed statusLed;
#endif
#ifdef DEBUG_STREAM
const byte SimulatedInput[] PROGMEM =
{
0xFF, 0xF9, 0xF0, 0x79, 0xF7, 0xF0, 0x6B, 0xF7, 0xF0, 0x69, 0xF7, 0xD0, 0x01, 0xD1, 0x01, 0xD2,
0x01, 0xF0, 0x7B, 0xFF, 0x05, 0x01, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x00, 0x01, 0x08, 0x02, 0x17,
0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x00, 0x04, 0x00, 0x00,
0x00, 0x02, 0x00, 0x03, 0x00, 0x58, 0x00, 0x2A, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x04, 0x00, 0x02,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7,
0xF0, 0x7B, 0xFF, 0x04, 0x00, 0x7F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x01, 0x01, 0x08, 0x02, 0x1A, 0x00,
0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x01, 0x05, 0x00, 0x00, 0x00,
0x02, 0x00, 0x03, 0x00, 0x7E, 0x01, 0x01, 0x00, 0x2A, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x04, 0x01,
0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0xF7, 0xF0, 0x7B, 0xFF, 0x04, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x02, 0x01, 0x08, 0x02, 0x13,
0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x02, 0x1B, 0x00, 0x00,
0x00, 0x03, 0x00, 0x1F, 0x00, 0x14, 0x00, 0x32, 0x00, 0x02, 0x00, 0x16, 0x00, 0x2A, 0x00, 0x02,
0x00, 0x03, 0x00, 0x31, 0x00, 0x02, 0x00, 0x16, 0x00, 0x2A, 0x00, 0x03, 0x00, 0x18, 0x00, 0x2E,
0x00, 0x02, 0x00, 0x16, 0x00, 0x2A, 0x00, 0x02, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x02, 0x1B,
0x00, 0x14, 0x00, 0x16, 0x00, 0x30, 0x00, 0x02, 0x00, 0x16, 0x00, 0x2A, 0x00, 0x17, 0x00, 0x2A,
0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7,
0xF0, 0x7B, 0xFF, 0x05, 0x01, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x00, 0x04, 0x04, 0x01, 0x49, 0x01,
0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x01, 0x0C, 0x01, 0x03, 0x4A,
0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x02, 0x0C, 0x02, 0x03,
0x4B, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x03, 0x04, 0x03,
0x02, 0x4C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x04, 0x0C,
0x05, 0x02, 0x4D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x05,
0x04, 0x06, 0x01, 0x4E, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01,
0x06, 0x0C, 0x07, 0x02, 0x4F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF,
0x01, 0x07, 0x09, 0x03, 0x02, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xF7, 0xF0, 0x7B,
0xFF, 0x03, 0x07, 0x3E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x16, 0x00, 0x30, 0x00, 0x0C, 0x00, 0x02,
0x00, 0x20, 0x00, 0x2A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0x00, 0x4F, 0x01, 0x00,
0x00, 0x00, 0x00, 0x06, 0x00, 0x2A, 0x00, 0x02, 0x00, 0x6F, 0x00, 0x49, 0x01, 0x00, 0x00, 0xF7,
0xF0, 0x7B, 0xFF, 0x03, 0x07, 0x3E, 0x00, 0x14, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0A, 0x00, 0x06,
0x00, 0x03, 0x00, 0x58, 0x00, 0x0B, 0x00, 0x06, 0x00, 0x07, 0x00, 0x31, 0x00, 0x1A, 0x00, 0x06,
0x00, 0x0C, 0x00, 0x2B, 0x00, 0x09, 0x00, 0x06, 0x00, 0x0C, 0x00, 0x02, 0x00, 0x6F, 0x00, 0x49,
0x01, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x07, 0x3E, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
0x00, 0x0A, 0x00, 0x08, 0x00, 0x06, 0x00, 0x32, 0x00, 0x73, 0x01, 0x2B, 0x00, 0x07, 0x00, 0x02,
0x00, 0x6F, 0x00, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0A, 0x00, 0x07, 0x00, 0x06,
0x00, 0x30, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x07, 0x3E, 0x00, 0x3C, 0x00, 0x75, 0x01, 0x2A,
0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x01, 0x08, 0x01, 0x07, 0x02, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00,
0x06, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x02, 0x08, 0x0C, 0x00, 0x00, 0x00, 0x4A, 0x01, 0x00, 0x00,
0x00, 0x00, 0x06, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x4B, 0x01, 0x00, 0x00,
0x00, 0x00, 0x06, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0xF7, 0xF0, 0x7B, 0xFF,
0x02, 0x08, 0x0C, 0x00, 0x04, 0x00, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01,
0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x4F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x01,
0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x02, 0x08, 0x0C, 0x00, 0x08, 0x00,
0x4C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x4E, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x00, 0x00, 0x16, 0x00, 0x0B, 0x00, 0x16, 0x00,
0x0C, 0x00, 0x1F, 0x00, 0x0A, 0x00, 0x0D, 0x00, 0x20, 0x00, 0x50, 0x01, 0x07, 0x00, 0x00, 0x00,
0x00, 0x00, 0x13, 0x00, 0x04, 0x00, 0x02, 0x00, 0x03, 0x00, 0x17, 0x00, 0x6F, 0x00, 0x7E, 0x00,
0x00, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x14, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x02, 0x00, 0x03, 0x00, 0x17, 0x00, 0x6F, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x02, 0x00, 0x09, 0x00, 0x17, 0x00, 0x6F, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x02, 0x00, 0x09, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x28, 0x00, 0x16, 0x00,
0x6F, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x02, 0x00, 0x1F, 0x00, 0x14, 0x00,
0x28, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x02, 0x00, 0x03, 0x00, 0x16, 0x00,
0x6F, 0x00, 0x7F, 0x00, 0x00, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x3C, 0x00,
0x00, 0x00, 0x0A, 0x00, 0x02, 0x00, 0x1F, 0x00, 0x14, 0x00, 0x28, 0x00, 0x00, 0x01, 0x00, 0x00,
0x00, 0x00, 0x0A, 0x00, 0x02, 0x00, 0x03, 0x00, 0x19, 0x00, 0x6F, 0x00, 0x7E, 0x00, 0x00, 0x00,
0x00, 0x00, 0x0A, 0x00, 0x11, 0x00, 0x04, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02,
0x50, 0x00, 0x0A, 0x00, 0x2B, 0x00, 0x14, 0x00, 0x06, 0x00, 0x25, 0x00, 0x17, 0x00, 0x59, 0x00,
0x0A, 0x00, 0x2D, 0x00, 0x0D, 0x00, 0x02, 0x00, 0x20, 0x00, 0x2A, 0x01, 0x2A, 0x01, 0x00, 0x00,
0x00, 0x00, 0x6F, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08,
0x28, 0x02, 0x64, 0x00, 0x0A, 0x00, 0x16, 0x00, 0x2A, 0x00, 0x02, 0x00, 0x03, 0x00, 0x6F, 0x00,
0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x2C, 0x00, 0x63, 0x01, 0x11, 0x00, 0x04, 0x00,
0x0A, 0x00, 0x2B, 0x00, 0x1C, 0x00, 0x06, 0x00, 0x25, 0x00, 0x17, 0x00, 0xF7, 0xF0, 0x7B, 0xFF,
0x03, 0x08, 0x28, 0x02, 0x78, 0x00, 0x59, 0x00, 0x0A, 0x00, 0x2D, 0x00, 0x15, 0x00, 0x02, 0x00,
0x09, 0x00, 0x17, 0x00, 0x6F, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x02, 0x00,
0x20, 0x00, 0x3B, 0x01, 0x3B, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6F, 0x00, 0x01, 0x01, 0xF7, 0xF0,
0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x16, 0x00,
0x2A, 0x00, 0x02, 0x00, 0x03, 0x00, 0x6F, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x17, 0x00, 0x2E, 0x00, 0x5A, 0x01, 0x02, 0x00, 0x09, 0x00, 0x17, 0x00, 0x6F, 0x00, 0x7F, 0x00,
0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x16, 0x00, 0x13, 0x00, 0x05, 0x00, 0x2B, 0x00, 0x78, 0x00, 0x11, 0x00, 0x04, 0x00, 0x0A, 0x00,
0x2B, 0x00, 0x11, 0x00, 0x06, 0x00, 0x25, 0x00, 0x17, 0x00, 0x59, 0x00, 0x0A, 0x00, 0x2D, 0x00,
0x0A, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x34, 0x01, 0x02, 0x00, 0x11, 0x00,
0x05, 0x00, 0x6F, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x16, 0x00, 0x2A, 0x00,
0x02, 0x00, 0x03, 0x00, 0x6F, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x2C, 0x00,
0x66, 0x01, 0x02, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x48, 0x01, 0x6F, 0x00,
0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x13, 0x00, 0x06, 0x00, 0x11, 0x00, 0x04, 0x00,
0x0A, 0x00, 0x2B, 0x00, 0x11, 0x00, 0x06, 0x00, 0x25, 0x00, 0x17, 0x00, 0x59, 0x00, 0x0A, 0x00,
0x2D, 0x00, 0x0A, 0x00, 0x02, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02, 0x5C, 0x01,
0x11, 0x00, 0x05, 0x00, 0x6F, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x16, 0x00,
0x2A, 0x00, 0x02, 0x00, 0x03, 0x00, 0x6F, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
0x17, 0x00, 0x2E, 0x00, 0x65, 0x01, 0x02, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08, 0x28, 0x02,
0x70, 0x01, 0x6F, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x11, 0x00, 0x06, 0x00,
0x59, 0x00, 0x07, 0x00, 0x17, 0x00, 0x62, 0x00, 0x0B, 0x00, 0x20, 0x00, 0x40, 0x00, 0x42, 0x00,
0x0F, 0x00, 0x00, 0x00, 0x5A, 0x00, 0x20, 0x00, 0x40, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x03, 0x08,
0x28, 0x02, 0x04, 0x02, 0x42, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x1F, 0x00, 0x1E, 0x00,
0x36, 0x00, 0x04, 0x00, 0x07, 0x00, 0x17, 0x00, 0x60, 0x00, 0x0B, 0x00, 0x11, 0x00, 0x05, 0x00,
0x1F, 0x00, 0x1F, 0x00, 0x33, 0x00, 0x04, 0x00, 0x07, 0x00, 0x0C, 0x00, 0xF7, 0xF0, 0x7B, 0xFF,
0x03, 0x08, 0x28, 0x02, 0x18, 0x02, 0x16, 0x00, 0x0B, 0x00, 0x11, 0x00, 0x05, 0x00, 0x17, 0x00,
0x58, 0x00, 0x13, 0x00, 0x05, 0x00, 0x11, 0x00, 0x05, 0x00, 0x1F, 0x00, 0x28, 0x00, 0x32, 0x00,
0x02, 0x01, 0x08, 0x00, 0x2A, 0x00, 0xF7, 0xF0, 0x7B, 0xFF, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7, 0xF0, 0x7B, 0xFF,
0x06, 0x08, 0xF7,
};

#include "FlashMemoryStream.h"
FlashMemoryStream debugStream(SimulatedInput, sizeof(SimulatedInput));
#endif

void systemResetCallback()
{
  for (byte i = 0; i < TOTAL_PINS; i++) {
    if (IS_PIN_ANALOG(i)) {
      Firmata.setPinMode(i, PIN_MODE_ANALOG);
    } else if (IS_PIN_DIGITAL(i)) {
      Firmata.setPinMode(i, OUTPUT);
    }
  }
  firmataExt.reset();
}

void initTransport()
{
  // Uncomment to save a couple of seconds by disabling the startup blink sequence.
  // Firmata.disableBlinkVersion();
#ifdef SIM
	NetworkSerial.begin();
	Firmata.begin(NetworkSerial);
#elif DEBUG_STREAM
    Firmata.begin(debugStream);
#else
    Firmata.begin(115200);
#endif
}

void initFirmata()
{
  // Set firmware name and version. The name is automatically derived from the name of this file.
  // Firmata.setFirmwareVersion(FIRMATA_FIRMWARE_MAJOR_VERSION, FIRMATA_FIRMWARE_MINOR_VERSION);
  // The usage of the above shortcut is not recommended, since it stores the full path of the file name in a 
  // string constant, using both flash and ram. 
  Firmata.setFirmwareNameAndVersion("ConfigurableFirmata", FIRMATA_FIRMWARE_MAJOR_VERSION, FIRMATA_FIRMWARE_MINOR_VERSION);

#ifdef ENABLE_DIGITAL
  firmataExt.addFeature(digitalInput);
  firmataExt.addFeature(digitalOutput);
#endif
	
#ifdef ENABLE_ANALOG
  firmataExt.addFeature(analogInput);
  firmataExt.addFeature(analogOutput);
#endif
	
#ifdef ENABLE_SERVO
  firmataExt.addFeature(servo);
#endif
	
#ifdef ENABLE_I2C
  firmataExt.addFeature(i2c);
#endif
	
#ifdef ENABLE_ONE_WIRE
  firmataExt.addFeature(oneWire);
#endif
	
#ifdef ENABLE_SERIAL
  firmataExt.addFeature(serial);
#endif
	
#ifdef ENABLE_BASIC_SCHEDULER
  firmataExt.addFeature(scheduler);
#endif
	
  firmataExt.addFeature(reporting);
#ifdef ENABLE_SPI
  firmataExt.addFeature(spi);
#endif
#ifdef ENABLE_ACCELSTEPPER
  firmataExt.addFeature(accelStepper);
#endif
	
#ifdef ENABLE_DHT
  firmataExt.addFeature(dhtFirmata);
#endif
	
#ifdef ENABLE_IL_EXECUTOR
  firmataExt.addFeature(ilExecutor);
  firmataExt.addFeature(statusLed);
#endif

  Firmata.attach(SYSTEM_RESET, systemResetCallback);
}

void setup()
{
	initTransport();
	Firmata.sendString(F("Booting device. Stand by..."));
	initFirmata();

	Firmata.parse(SYSTEM_RESET);

	Firmata.sendString(F("System booted. Free bytes: 0x"), freeMemory());
}

void loop()
{
  while(Firmata.available()) {
    Firmata.processInput();
    if (!Firmata.isParsingMessage()) {
      break;
    }
  }

  firmataExt.report(reporting.elapsed());
}
