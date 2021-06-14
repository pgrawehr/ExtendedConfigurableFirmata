// RtcBase.h

#ifndef _RTCBASE_h
#define _RTCBASE_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif
#include "SystemException.h"
#include "Exceptions.h"

using namespace stdSimple;

static const int s_daysToMonth365[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
static const int s_daysToMonth366[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };

class RtcBase
{
public:
	virtual void Init()
	{
	}

	virtual ~RtcBase()
	{
	}

	int64_t GetAsDateTimeTicks(int year, int month, int day, int hour, int minute, int second, int millis)
	{
		return DateToTicks(year, month, day) + TimeToTicks(hour, minute, second) + (millis * TicksPerMillisecond);
	}

	byte BcdToDecimal(const byte input)
	{
		byte lower = input & 0x0F;
		byte upper = (input & 0xF0) >> 4;
		return upper * 10 + lower;
	}

	virtual int64_t ReadTime() = 0;

private:
	static const int64_t TicksPerMillisecond = 10000;
	static const int64_t TicksPerSecond = TicksPerMillisecond * 1000;
	static const int64_t TicksPerMinute = TicksPerSecond * 60;
	static const int64_t TicksPerHour = TicksPerMinute * 60;
	static const int64_t TicksPerDay = TicksPerHour * 24;
	static const int64_t MaxSeconds = 9223372036854775807 / TicksPerSecond;
	static const int64_t MinSeconds = -922337203685; // -9223372036854775808i64 / TicksPerSecond; // C++ doesn't accept that constant as valid for 64 bit

	static bool IsLeapYear(int year)
	{
		// Magic taken from DateTime.cs
		return (year & 3) == 0 && ((year & 15) == 0 || (year % 25) != 0);
	}
	static int64_t DateToTicks(int year, int month, int day)
	{
		if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1)
		{
			throw ClrException("Invalid date", SystemException::InvalidOperation, 0);
		}

		const int* days = IsLeapYear(year) ? s_daysToMonth366 : s_daysToMonth365;
		if (day > days[month] - days[month - 1])
		{
			throw ClrException("Invalid date", SystemException::InvalidOperation, 0);
		}

		int y = year - 1;
		int n = y * 365 + y / 4 - y / 100 + y / 400 + days[month - 1] + day - 1;
		return n * TicksPerDay;
	}

	static int64_t TimeToTicks(int hour, int minute, int second)
	{
		// totalSeconds is bounded by 2^31 * 2^12 + 2^31 * 2^8 + 2^31,
		// which is less than 2^44, meaning we won't overflow totalSeconds.
		long totalSeconds = (long)hour * 3600 + (long)minute * 60 + (long)second;
		if (totalSeconds > MaxSeconds || totalSeconds < MinSeconds)
		{
			throw stdSimple::ClrException("Invalid time", SystemException::InvalidOperation, 0);
		}

		return totalSeconds * TicksPerSecond;
	}
	
};
#endif

