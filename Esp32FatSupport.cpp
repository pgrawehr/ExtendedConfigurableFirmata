// 
// 
//

#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "Esp32FatSupport.h"
#include "Exceptions.h"

#ifdef ESP32
#include <FS.h>
#include <FFat.h>

Esp32FatSupport::Esp32FatSupport()
{
}

void Esp32FatSupport::Init()
{
	if (!FFat.begin())
	{
		Firmata.sendString(F("Unable to mount FFat partition. Attempting format"));
		FFat.format();
		if (!FFat.begin())
		{
			throw stdSimple::ExecutionEngineException("Unable to mount FAT partition. Aborting");
		}
	}

	// Make sure the temp directory exists
	FFat.mkdir("/tmp");
	
	Firmata.sendStringf(F("Total space on data partition: %10u\n"), 8, FFat.totalBytes());
	Firmata.sendStringf(F("Free space on data partition: %10u\n"), 8, FFat.freeBytes());
}

bool Esp32FatSupport::ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	return false;
}


void Esp32FatSupport::Update()
{
	// Nothing to do here
}


Esp32FatSupport::~Esp32FatSupport()
{
	// Nothing to do
}

#endif
