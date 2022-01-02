// FtpServer.h

#ifndef _FTPSERVER_h
#define _FTPSERVER_h

#include <ConfigurableFirmata.h>
#ifdef ESP32
class FtpServerClass
{
 protected:


 public:
	void Init();

private:
	TaskHandle_t ftpTaskHandle;
};

extern FtpServerClass FtpServer;

#endif
#endif
