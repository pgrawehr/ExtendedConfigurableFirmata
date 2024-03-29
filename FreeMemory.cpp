
#include "ConfigurableFirmata.h"
#include "FreeMemory.h"

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#elif defined (ESP32)
// Nothing, the ESP has predefined functions for this
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

#if SIM
int freeMemory()
{
	// More than we need, anyway
	return INT32_MAX;
}

#else

// Check current SRAM occupation 
int freeMemory() 
{
#if defined(__arm__)
	char top;
    return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(ESP32)
  // has sbrk, but calling it seems to cause a crash
    return ESP.getFreeHeap();
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
	char top;
    return &top - __brkval;

#else  // __arm__
	char top;
    return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}
#endif

void printMemoryStatistics()
{
	Firmata.sendStringf(F("Free heap: %dkb"), (uint32_t)freeMemory() / 1024);
#ifdef ESP32
	Firmata.sendStringf(F("Total heap: %dkb"), ESP.getHeapSize() / 1024);
	Firmata.sendStringf(F("Free PSRAM: %dkb"), ESP.getFreePsram() / 1024);
	Firmata.sendStringf(F("Total PSRAM: %dkb"), ESP.getPsramSize() / 1024);
	Firmata.sendStringf(F("Minimum free heap since boot: %d Bytes"), ESP.getMinFreeHeap());
	Firmata.sendStringf(F("Maximum block that can be allocated: %d bytes"), ESP.getMaxAllocHeap());
#endif
}
