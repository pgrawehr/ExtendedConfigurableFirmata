/*
  DhtFirmata.h - Firmata library

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  See file LICENSE.txt for further informations on licensing terms.

*/

#ifndef DhtFirmata_h
#define DhtFirmata_h

#include "ConfigurableFirmata.h"

#define DHT_SENSOR_DATA_REQUEST (0x02) // User defined data
#include "FirmataFeature.h"
#include "FirmataReporting.h"

#include <DHT.h>
 union dhtData
 {
    short shorts[2];
    byte bytes[4];
 };


class DhtFirmata: public FirmataFeature
{
  public:
    DhtFirmata();
    boolean handlePinMode(byte pin, int mode);
    void handleCapability(byte pin);
    boolean handleSysex(byte command, byte argc, byte* argv);
    void reset();
    void report();

  private:
    void performDhtTransfer(byte command, byte argc, byte *argv);
    void disableDht();
    bool isDhtEnabled;
    int dhtPin; // Only one sensor supported
    DHT* dht;
};


DhtFirmata::DhtFirmata()
{
  isDhtEnabled = false;
  dhtPin = -1;
}

boolean DhtFirmata::handlePinMode(byte pin, int mode)
{
  if (IS_PIN_DIGITAL(pin)) {
    if (mode == PIN_MODE_DHT) {
      return true;
    }
	else if (isDhtEnabled) 
	{
		disableDht();
	}
  }
  return false;
}

void DhtFirmata::handleCapability(byte pin)
{
  if (IS_PIN_DIGITAL(pin)) {
    Firmata.write(PIN_MODE_DHT);
    Firmata.write(64); // 2x 32 Bit data per measurement
  }
}

boolean DhtFirmata::handleSysex(byte command, byte argc, byte *argv)
{
  switch (command) {
    case DHT_SENSOR_DATA_REQUEST:
		  if (argc < 2)
		  {
			  Firmata.sendString(F("Error in DHT command: Not enough parameters"));
			  return false;
		  }
        performDhtTransfer(argv[0], argc - 1, argv + 1);
        return true;
  }
  return false;
}

void DhtFirmata::performDhtTransfer(byte command, byte argc, byte *argv)
{
	// command byte: DHT Type
	byte dhtType = command;
	// first byte: pin
	byte pin = argv[0];
	if (!isDhtEnabled)
	{
		dhtPin = pin;
		isDhtEnabled = true;
		dht = new DHT(pin, dhtType);
		dht->begin();
	}
	else if (dhtPin != pin)
	{
		Firmata.sendString(F("DHT error: Different pin specified after init"));
		return;
	}
	
	dhtData dataUnion;
	// Accuracy of the sensor is very limited, only 8 bit humidity and maybe 10 bit for temperature
	dataUnion.shorts[0] = (short)dht->readHumidity();
	dataUnion.shorts[1] = (short)(dht->readTemperature() * 10);
	
	Firmata.startSysex();
	Firmata.write(DHT_SENSOR_DATA_REQUEST);
	Firmata.write(dhtType);
	Firmata.write(pin);
	for (int i = 0; i < sizeof(dhtData); i++)
	{
		Firmata.sendValueAsTwo7bitBytes(dataUnion.bytes[i]);
	}
	Firmata.endSysex();
}

void DhtFirmata::disableDht()
{
	delete dht;
	dht = NULL;
	isDhtEnabled = false;
}

void DhtFirmata::reset()
{
  if (isDhtEnabled) {
	disableDht();
  }
}

void DhtFirmata::report()
{
}

#endif 
