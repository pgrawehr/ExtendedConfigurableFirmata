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

	/// <summary>
	/// This represents an out of memory error (both managed or unmanaged memory)
	/// This error is unrecoverable for now.
	/// </summary>
	class OutOfMemoryException : public Exception
	{
	public:
		OutOfMemoryException(const char* msg);

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
