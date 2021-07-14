
#include "ConfigurableFirmata.h"
#include "FreeMemory.h"

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#elif defined (ESP32)
extern "C" void* sbrk(ptrdiff_t __incr);
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
  char top;
#if defined(__arm__)
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(ESP32)
  // has sbrk, but calling it seems to cause a crash
  char* m = (char*)malloc(10);
  int32_t freeMem = m - &top;
  free(m);
  return freeMem;
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;

#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}
#endif
