#include <ConfigurableFirmata.h>
#include "ArduinoDueSupport.h"

#ifdef ARDUINO_DUE
void* xSemaphoreCreateBinary()
{
	return (void*)0x100FFFFF; // Some dummy handle
}

extern "C" unsigned char _etext;
// See https://arduino.stackexchange.com/questions/83911/how-do-i-get-the-size-of-my-program-at-runtime/83916#83916 for
// an explanation of this function
long SizeOfApp()
{
	byte* rom_end = &_etext;
	rom_end = (byte*)(((uint32_t)rom_end + 256) & ~0xFF); // Align to next free flash block (even if the memory ends right on a boundary)
	return (long)rom_end;
}


#endif

