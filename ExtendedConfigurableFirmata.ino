/*
 * ExtendedConfigurableFirmata startup code
 */

#include <ConfigurableFirmata.h>

#include "FreeMemory.h"

// Use this to enable WIFI instead of serial communication. Tested on ESP32, but should also
// work with Wifi-enabled Arduinos
#define ENABLE_WIFI

#if __has_include("wifiConfig.h")
#include "wifiConfig.h"
#else
const char* ssid     = "your-ssid";
const char* password = "your-password";
const int NETWORK_PORT = 27016;
#endif

// Use these defines to easily enable or disable certain modules

/* Note: Currently no client support by dotnet/iot for these, so they're disabled by default */
/* Enabling all modules on the smaller Arduino boards (such as the UNO or the Nano) won't work anyway, as there is both
 * not enough flash as well as not enough RAM. 
 */

// #define ENABLE_ONE_WIRE
// Note that the SERVO module currently is not supported on ESP32. So either disable this or patch the library
#ifndef ESP32
#define ENABLE_SERVO 
#endif
// #define ENABLE_ACCELSTEPPER
// #define ENABLE_BASIC_SCHEDULER
#define ENABLE_SERIAL

/* Native reading of DHTXX sensors. Reading a DHT11 directly using GPIO methods from a remote PC will not work, because of the very tight timing requirements of these sensors*/
#ifndef SIM
#define ENABLE_DHT
#endif
#define ENABLE_I2C
// #define ENABLE_IL_EXECUTOR
#define ENABLE_SPI
#define ENABLE_ANALOG
#define ENABLE_DIGITAL
#define ENABLE_FREQUENCY

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


#ifdef ENABLE_WIFI
#include <WiFi.h>
#include "utility/WiFiClientStream.h"
#include "utility/WiFiServerStream.h"
WiFiServerStream serverStream(NETWORK_PORT);
#endif

#ifdef ENABLE_I2C
#include <Wire.h>
#include <I2CFirmata.h>
I2CFirmata i2c;
#endif

#ifdef ENABLE_SPI
#include <Wire.h>
#include <SpiFirmata.h>
SpiFirmata spi;
#endif

#ifdef ENABLE_ONE_WIRE
#include <OneWireFirmata.h>
OneWireFirmata oneWire;
#endif

#ifdef ENABLE_SERIAL
#include <SerialFirmata.h>
SerialFirmata serial;
#endif

#ifdef ENABLE_DHT
#include <DhtFirmata.h>
DhtFirmata dhtFirmata;
#endif

#include <FirmataExt.h>
FirmataExt firmataExt;

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

#ifdef ENABLE_FREQUENCY
#include <Frequency.h>
Frequency frequency;
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

void systemResetCallback()
{
  for (byte i = 0; i < TOTAL_PINS; i++) {
    if (IS_PIN_ANALOG(i)) {
      Firmata.setPinMode(i, PIN_MODE_ANALOG);
    } else if (IS_PIN_DIGITAL(i)) {
      Firmata.setPinMode(i, PIN_MODE_INPUT);
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
#elif defined(ENABLE_WIFI)
	Firmata.begin(115200); // To make sure the serial port is also available
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	pinMode(16, OUTPUT);
	bool pinIsOn = false;
	while (WiFi.status() != WL_CONNECTED)
	{
	    delay(100);
	    pinIsOn = !pinIsOn;
	    digitalWrite(16, pinIsOn);
	}
	digitalWrite(16, 0);
	Firmata.begin(serverStream);
	Firmata.blinkVersion(); // Because the above doesn't do it.
	Firmata.sendString(F("WIFI connection established"));
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

#ifdef ENABLE_FREQUENCY
  firmataExt.addFeature(frequency);
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

#ifdef ENABLE_IL_EXECUTOR
	ilExecutor.Init();
#endif
	Firmata.sendString(F("System booted. Free bytes: 0x"), freeMemory());
}

void loop()
{
  while(Firmata.available()) {
#ifdef ENABLE_IL_EXECUTOR
	if (statusLed.getStatus() == STATUS_IDLE)
	{
		statusLed.setStatus(STATUS_COMMANDED, 1000);
	}
#endif
    Firmata.processInput();
    if (!Firmata.isParsingMessage()) {
      break;
    }
  }

  firmataExt.report(reporting.elapsed());
#ifdef ENABLE_WIFI
  serverStream.maintain();
#endif
}
