#pragma once
#include <ConfigurableFirmata.h>
#if defined __SAM3X8E__ && !defined SIM
#define ARDUINO_DUE
#endif
#ifdef ARDUINO_DUE

/* RTOS implementation functions */
void* xSemaphoreCreateBinary();

#endif
