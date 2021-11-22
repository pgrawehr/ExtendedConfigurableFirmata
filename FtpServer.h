// FtpServer.h

#ifndef _FTPSERVER_h
#define _FTPSERVER_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

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

