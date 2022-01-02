#include <ConfigurableFirmata.h>
#include "ArduinoDueSupport.h"

#ifdef __SAM3X8E__
void* xSemaphoreCreateBinary()
{
	return (void*)0x100FFFFF; // Some dummy handle
}
#endif

