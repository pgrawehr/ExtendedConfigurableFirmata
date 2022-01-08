#include <ConfigurableFirmata.h>
#include "ArduinoDueSupport.h"

#if defined __SAM3X8E__ && !defined SIM
#define ARDUINO_DUE
void* xSemaphoreCreateBinary()
{
	return (void*)0x100FFFFF; // Some dummy handle
}
#endif

