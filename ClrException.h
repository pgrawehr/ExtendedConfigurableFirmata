#pragma once

#include "Exceptions.h"

// Forward declare, to prevent a circular dependency
class FirmataIlExecutor;

class ClrException : public stdSimple::Exception
{
private:
	int _tokenCausingException;
	SystemException _exceptionType;

public:
	ClrException(SystemException exceptionType, int exceptionToken);
	ClrException(const char* msg, SystemException exceptionType, int exceptionToken);

	SystemException ExceptionType()
	{
		return _exceptionType;
	}

	int ExceptionToken()
	{
		return _tokenCausingException;
	}

	virtual void* ExceptionObject(FirmataIlExecutor* executor);

private:
	const char* GetExceptionText(SystemException systemException);
};
