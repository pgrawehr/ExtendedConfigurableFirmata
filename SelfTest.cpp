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
	const int oneK = 1024;
	const int maxMemToTest = 100; // the arduino due has 96k of memory
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

	ASSERT(totalAllocsSucceeded > 64, "Not enough free memory after init");
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
	int64_t* ptr = (int64_t*)malloc(20);
	*ptr = -1;
	int64_t* ptr2 = ptr;
	ASSERT(*ptr2 == *ptr, "Pointer access error");
	ptr = AddBytes(ptr, 4);
	*ptr = 10;
	ptr2 = ptr;
	ASSERT(*ptr2 == *ptr, "Unable to read 8-byte values from 4-byte aligned addresses");

	volatile int* iPtr = (int*)AddBytes(ptr, 1);
	*iPtr = 5;
	ASSERT(*iPtr == 5, "Error in unaligned memory access");
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

