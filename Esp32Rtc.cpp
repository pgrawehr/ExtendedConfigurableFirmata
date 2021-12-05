// 
// 
// 

#include <ConfigurableFirmata.h>
#include <time.h>
#include <sys/time.h>

#include "Esp32Rtc.h"

void Esp32Rtc::Init()
{
}

// The ESP32 has a built-in real-time clock, but without battery. When using a Wifi connection, it uses NTP to sync the clock,
// otherwise we would need a TODO to send the current time when starting (or use a DS1307 during startup)
int64_t Esp32Rtc::ReadTime()
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	time(&now);
	localtime_r(&now, &timeinfo);
	_previousValue = GetAsDateTimeTicks(1900 + timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, 0);
	return _previousValue;
}
