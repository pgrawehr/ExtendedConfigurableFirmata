// 
// 
// 

#include <ConfigurableFirmata.h>
#include "NtpClient.h"


#include <time.h>
#include <sys/time.h>

// This one is ESP specific
#ifdef ESP32
#include "esp_sntp.h"

NtpClient::NtpClient()
{
}

void NtpClient::StartTimeSync(int blinkPin)
{
	Serial.println("Initializing SNTP");
	setenv("TZ", "UTC", 0);
	tzset();
	esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
	esp_sntp_setservername(0, const_cast<char*>("pool.ntp.org"));
	esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
	esp_sntp_init();

	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 0;
	bool pinIsOn = false;
	const int retry_count = 100;
	while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
		ESP_LOGI("NTP", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		delay(100);
		if (blinkPin >= 0)
		{
			pinIsOn = !pinIsOn;
			digitalWrite(blinkPin, pinIsOn);
		}
	}

	ESP_LOGI("NTP", "Time synchronization complete");
	esp_sntp_stop();
	char strftime_buf[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI("NTP", "The current date / time in UTC is: %s", strftime_buf);
	if (blinkPin >= 0)
	{
		digitalWrite(blinkPin, 0);
	}
}

#endif
