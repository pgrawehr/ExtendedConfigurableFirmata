// Exceptions.h

#ifndef _EXCEPTIONS_h
#define _EXCEPTIONS_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "ConfigurableFirmata.h"
enum class SystemException;

namespace stdSimple
{
	class Exception
	{
	private:
		char _msg[200];
	public:
		Exception(const char* fmt, ...);

		const char* Message() const
		{
			return _msg;
		}
	};

	class ClrException : public Exception
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

	};

	class OutOfMemoryException : public ClrException
	{
	public:
		OutOfMemoryException();

		/// <summary>
		/// This one is pre-allocated because we obviously cannot create a new instance when we run into this problem
		/// </summary>
		static OutOfMemoryException OutOfMemoryExceptionInstance;

		static void Throw()
		{
			throw OutOfMemoryExceptionInstance;
		}
	};

	class ExecutionEngineException: public Exception
	{
	public:
		ExecutionEngineException(const char* msg);
	};

	
}


#endif

