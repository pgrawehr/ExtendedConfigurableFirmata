
#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "ClrException.h"
#include "FirmataIlExecutor.h"



ClrException::ClrException(SystemException exceptionType, int exceptionToken)
	: Exception("")
{
	_exceptionType = exceptionType;
	_tokenCausingException = exceptionToken;
}

ClrException::ClrException(const char* msg, SystemException exceptionType, int exceptionToken)
	: Exception(msg)
{
	_exceptionType = exceptionType;
	_tokenCausingException = exceptionToken;
}

/// <summary>
/// Creates a CLR exception from a system exception
/// </summary>
/// <returns>A managed pointer</returns>
void* ClrException::ExceptionObject()
{
	return nullptr;
}
