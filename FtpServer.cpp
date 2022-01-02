// 
// 
// 
#include <ConfigurableFirmata.h>
#include "FtpServer.h"

#ifdef ESP32
void ftp_task(void* args);

void FtpServerClass::Init()
{
	xTaskCreatePinnedToCore(ftp_task, "FTP", 1024 * 6, NULL, 2, &ftpTaskHandle, 0);
}


FtpServerClass FtpServer;

#endif
