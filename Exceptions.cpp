// 
// 
// 

#include "ConfigurableFirmata.h"
#include "Exceptions.h"

#include <stdarg.h>

namespace stdSimple
{
	Exception* Exception::OutOfMemoryException = new Exception("SEVERE: Out of memory.");

	Exception::Exception(const char* fmt, int sizeOfArgs, ...)
	{
		_ownMsg = true;
		int len = strlen(fmt);
		len += sizeOfArgs * 6; // for the formmatted args
		va_list va;
		va_start(va, sizeOfArgs);
		char* bytesOutput = (char*)malloc(len);
		memset(bytesOutput, 0, len);

		vsnprintf(bytesOutput, len, fmt, va);
		bytesOutput[len - 1] = 0;
		
		va_end(va);

		_msg = bytesOutput;
		Firmata.sendString(STRING_DATA, bytesOutput);
	}

	Exception::Exception(const char* msg)
	{
		_msg = const_cast<char*>(msg);
		_ownMsg = false;
	}

	const char* Exception::Message() const
	{
		if (_msg != nullptr)
		{
			return _msg;
		}
		
		return "No message given";
	}

}
