// Exceptions.h

#ifndef _EXCEPTIONS_h
#define _EXCEPTIONS_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "ConfigurableFirmata.h"
namespace stdSimple
{
	class Exception
	{
	private:
		char* _msg;
		bool _ownMsg; // true if msg is a dynamically allocated instance
	public:
		Exception(const char* msg);
		Exception(const char* fmt, int sizeOfArgs, ...);

		~Exception()
		{
			if (_ownMsg && _msg)
			{
				free(_msg);
				_msg = nullptr;
			}
		}

		const char* Message() const;

		/// <summary>
		/// This one is pre-allocated because we obviously cannot create a new instance when we run into this problem
		/// </summary>
		static Exception* OutOfMemoryException;
	};
}


#endif

