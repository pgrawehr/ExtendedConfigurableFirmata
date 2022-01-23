#include "ConfigurableFirmata.h"
#include "SelfTest.h"
#include "Variable.h"
#include "VariableVector.h"
#include "VariableDynamicStack.h"

void ASSERT(bool x)
{
	if (!x)
	{
		throw stdSimple::ExecutionEngineException("Assertion failed");
	}
}

void ASSERT(bool condition, const char* message)
{
	if (!condition)
	{
		throw stdSimple::ExecutionEngineException(message);
	}
}

bool SelfTest::PerformSelfTest()
{
	_statusFlag = true;
	PerformMemoryAnalysis();
	// If this fails a second time, the memory management is broken
	PerformMemoryAnalysis();
	ValidateMemoryManager();
	// ValidateMemoryManager();
	ValidateExecutionStack();
	UnalignedAccessWorks();
	CompilerBehavior();
	return _statusFlag;
}

void SelfTest::PerformMemoryAnalysis()
{
	const int SIZE_TO_TEST = 1024 * 70;
	int* mem = (int*)malloc(SIZE_TO_TEST);

	for (int i = 0; i < SIZE_TO_TEST / 4; i++)
	{
		mem[i] = i;
	}

	for (int i = 0; i < SIZE_TO_TEST / 4; i++)
	{
		if (mem[i] != i)
		{
			// You better get a new board if this happens
			Firmata.sendString(F("HARDWARE ERROR: Memory broken"));
			_statusFlag = false;
			free(mem);
			return;
		}
	}

	free(mem);
}

void SelfTest::ValidateMemoryManager()
{
#if !defined(SIM)
	void* data = malloc(1024 * 1024);
	// Need to patch the C++ library to fix a runtime bug that exists in the C runtime of the Arduino Due:
	// There's no limit to the amount of memory that can be allocated, the CPU just crashes if you try to use it.
	ASSERT(data == nullptr, "Memory allocation error: Can allocate more memory than available. Please consult the documentation");
#endif
	const int oneK = 1024;
#if ESP32
	const int maxMemToTest = 1024; // The esp32 has some 300k of RAM
#else
	const int maxMemToTest = 100; // the arduino due has 96k of memory
#endif
	void* ptrs[maxMemToTest];
	int idx = 0;
	int totalAllocsSucceeded = 0;
	while (idx < maxMemToTest)
	{
		void* mem = malloc(oneK);
		ptrs[idx] = mem;
		if (mem == nullptr)
		{
			break;
		}
		idx++;
	}

	// Happens in simulation
	if (idx == maxMemToTest)
	{
		idx--;
	}
	
	totalAllocsSucceeded = idx;
	while (idx >= 0)
	{
		free(ptrs[idx]);
		idx--;
	}

	Firmata.sendStringf(F("Total memory available after init: %dkb"), totalAllocsSucceeded);
	
	ASSERT(totalAllocsSucceeded >= 82, "Not enough free memory after init");

	// Validate this variable has the correct size
	ASSERT(sizeof(Variable) == 12, "Size of Variable type is not correct. Ensure the compiler uses 1-byte struct packing");
	Variable temp;
	byte* startAddr = (byte*) &temp;
	byte* dataAddr = (byte*)&temp.Int32;
	ASSERT(dataAddr - startAddr == 4, "Size of Variable type is not correct. Ensure the compiler uses 1-byte struct packing");
}

void SelfTest::ValidateExecutionStack()
{
	VariableDynamicStack st(10);
	Variable a;
	a.Type = VariableKind::Int32;
	a.Int32 = 10;
	st.push(a);
	Variable b = st.top();
	ASSERT(b.Int32 == 10, "Element is not at top of stack");
	st.pop();
	ASSERT(st.empty(), "Stack is not empty");

	b.Int32 = 0xFFFF;
	st.push(b);
	Variable c = st.top();
	st.pop();
	ASSERT(b.Int32 == c.Int32, "Element not found");
	
	st.push(b);
	st.pop();
	st.push(c);
	ASSERT(b.Int32 == 0xFFFF, "Internal selftest error: Stack overwrites instances");
	st.pop();
	st.push(a);
	st.push(b);
	c = st.nth(1);
	ASSERT(c.Int32 == a.Int32, "Internal selftest error: Stack count doesn't fit");
}

void SelfTest::UnalignedAccessWorks()
{
	int64_t* ptrStart = (int64_t*)malloc(64);
	volatile int64_t* ptr = ptrStart;
	*ptr = -1;
	volatile int64_t* ptr2 = ptr;
	ASSERT(*ptr2 == *ptr, "Pointer access error");
	ptr = AddBytes(ptr, 4);
	*ptr = 10;
	ptr2 = ptr;
	ASSERT(*ptr2 == *ptr, "Unable to read 8-byte values from 4-byte aligned addresses");

	volatile int* iPtr = (int*)AddBytes(ptr, 1);
	*iPtr = 5;
	ASSERT(*iPtr == 5, "Error in unaligned memory access");

	// 64 bit values can only be read from 4-byte aligned addresses on the Cortex-M3 (Arduino due) it seems. Changing the constant below to 1 or 2 crashes the CPU
	ptr = AddBytes(ptrStart, 4);
	*ptr = 10;

	ASSERT(*ptr == 10, "64 Bit memory access error");
	
	free(ptrStart);

	int* data = (int*)malloc(64);

	int* data2 = AddBytes(data, 2);

	*data2 = 4711;
	
	free(data);
}

void SelfTest::CompilerBehavior()
{
	/*int a = 2;
	int b = 5;
	int& ref = a;
	ref = 3;
	ASSERT(a == 3, "Reference behavior error");
	ref = b;
	ref = 7;
	ASSERT(a == 3, "Reference behavior error");
	ASSERT(b == 7, "Reference behavior error");*/
}
