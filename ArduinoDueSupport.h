#pragma once
#include <ConfigurableFirmata.h>

#ifdef __SAM3X8E__

/* RTOS implementation functions */
void* xSemaphoreCreateBinary();

#endif
