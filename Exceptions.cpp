// 
// 
// 

#include "ConfigurableFirmata.h"
#include "Exceptions.h"
#include "FirmataIlExecutor.h"

#include <stdarg.h>

namespace stdSimple
{

	Exception::Exception(const char* fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		memset(_msg, 0, sizeof(_msg));

		vsnprintf(_msg, sizeof(_msg), fmt, va);
		_msg[sizeof(_msg) - 1] = 0;
		
		va_end(va);
	}

	OutOfMemoryException::OutOfMemoryException(const char* msg)
		: Exception(msg, SystemException::OutOfMemory, 0)
	{
	}
	
	ExecutionEngineException::ExecutionEngineException(const char* msg)
		: Exception(msg)
	{
	}

	
}
