// 
// 
// 
#include <ConfigurableFirmata.h>
#include "FtpServer.h"

void ftp_task(void* args);

void FtpServerClass::Init()
{
	xTaskCreatePinnedToCore(ftp_task, "FTP", 1024 * 6, NULL, 2, &ftpTaskHandle, 0);
}


FtpServerClass FtpServer;

