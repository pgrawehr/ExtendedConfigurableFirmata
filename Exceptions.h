// Exceptions.h
#pragma once

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
		OutOfMemoryException(const char *);

		static void Throw(const char* msg)
		{
			throw OutOfMemoryException(msg);
		}
	};

	class ExecutionEngineException: public Exception
	{
	public:
		ExecutionEngineException(const char* msg);
	};

	
}

