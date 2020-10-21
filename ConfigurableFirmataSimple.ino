/*
 * CommonFirmataFeatures.ino generated by FirmataBuilder
 * Sun Mar 29 2020 15:10:48 GMT-0400 (EDT)
 */

#include "ConfigurableFirmata.h"

#include <Wire.h>
#include "AnalogInputFirmata.h"
#include "AnalogOutputFirmata.h"
#include "DigitalInputFirmata.h"
#include "DigitalOutputFirmata.h"
#include "FirmataExt.h"
#include "FirmataIlExecutor.h"
#include "FirmataScheduler.h"
#include "I2CFirmata.h"
#include "SerialFirmata.h"
#include "SpiFirmata.h"
DigitalInputFirmata digitalInput;
DigitalOutputFirmata digitalOutput;

AnalogInputFirmata analogInput;

I2CFirmata i2c;

//#include <OneWireFirmata.h>
//OneWireFirmata oneWire;

SerialFirmata serial;

FirmataExt firmataExt;

SpiFirmata spi;

//#include <Servo.h>
//#include <ServoFirmata.h>
//ServoFirmata servo;


// The scheduler allows to store scripts on the board, however this requires a kind of compiler on the client side.
// The feature only needs 20Bytes of Ram, so it doesn't hurt to have it (There's enough flash left)
FirmataScheduler scheduler;

FirmataReporting reporting;

// #include <AccelStepperFirmata.h>
// AccelStepperFirmata accelStepper;

// #include <DhtFirmata.h>
// DhtFirmata dhtFirmata;

FirmataIlExecutor ilExecutor;

void systemResetCallback()
{
  for (byte i = 0; i < TOTAL_PINS; i++) {
    if (IS_PIN_ANALOG(i)) {
      Firmata.setPinMode(i, ANALOG);
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
  Firmata.begin(115200);
}

void initFirmata()
{
  Firmata.setFirmwareVersion(FIRMATA_FIRMWARE_MAJOR_VERSION, FIRMATA_FIRMWARE_MINOR_VERSION);

  firmataExt.addFeature(digitalInput);
  firmataExt.addFeature(digitalOutput);
  firmataExt.addFeature(analogInput);
  // firmataExt.addFeature(servo);
  firmataExt.addFeature(i2c);
  // firmataExt.addFeature(oneWire);
  firmataExt.addFeature(serial);
  firmataExt.addFeature(scheduler);
  firmataExt.addFeature(reporting);
  firmataExt.addFeature(spi);
  // firmataExt.addFeature(accelStepper);
  // firmataExt.addFeature(dhtFirmata);
  firmataExt.addFeature(ilExecutor);

  Firmata.attach(SYSTEM_RESET, systemResetCallback);
}

void setup()
{
  initFirmata();

  initTransport();

  Firmata.parse(SYSTEM_RESET);
}

void loop()
{
  digitalInput.report();

  while(Firmata.available()) {
    Firmata.processInput();
    if (!Firmata.isParsingMessage()) {
      goto runtasks;
    }
  }
  if (!Firmata.isParsingMessage()) {
runtasks: scheduler.runTasks();
  }
  
  if (reporting.elapsed()) {
    analogInput.report();
    i2c.report();
  }

  // accelStepper.update();
  serial.update();
}
