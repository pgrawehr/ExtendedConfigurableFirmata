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
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, const_cast<char*>("pool.ntp.org"));
	sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
	sntp_init();

	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 0;
	bool pinIsOn = false;
	const int retry_count = 100;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		delay(100);
		if (blinkPin >= 0)
		{
			pinIsOn = !pinIsOn;
			digitalWrite(blinkPin, pinIsOn);
		}
	}

	ESP_LOGI(TAG, "Time synchronization complete");
	sntp_stop();
	char strftime_buf[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "The current date / time in UTC is: %s", strftime_buf);
	if (blinkPin >= 0)
	{
		digitalWrite(blinkPin, 0);
	}
}

#endif
