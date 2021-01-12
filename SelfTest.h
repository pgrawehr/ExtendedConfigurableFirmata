﻿// SelfTest.h

#ifndef _SELFTEST_h
#define _SELFTEST_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "Exceptions.h"

class SelfTest
{
	bool _statusFlag;
public:
	SelfTest()
	{
		_statusFlag = true;
	}
	
	bool PerformSelfTest();
private:

	void PerformMemoryAnalysis();
	void ValidateMemoryManager();

	void ValidateExecutionStack();

	void UnalignedAccessWorks();
	void CompilerBehavior();
};
#endif

void ASSERT(bool x);
void ASSERT(bool condition, const char* message);
