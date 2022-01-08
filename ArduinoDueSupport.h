#pragma once
#include <ConfigurableFirmata.h>
#include "ArduinoDueSupport.h"

#ifdef ARDUINO_DUE

/* RTOS implementation functions */
void* xSemaphoreCreateBinary();

#endif
