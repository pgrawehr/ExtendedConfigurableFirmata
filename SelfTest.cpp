#include "ConfigurableFirmata.h"
#include "SelfTest.h"
#include "Variable.h"
#include "VariableContainer.h"
#include "VariableDynamicStack.h"

#define ASSERT(x, msg) if (!(x)) \
	{\
		Firmata.sendString(F(msg));\
		_statusFlag = false;\
		return;\
	}

bool SelfTest::PerformSelfTest()
{
	_statusFlag = true;
	PerformMemoryAnalysis();
	ValidateExecutionStack();
	UnalignedAccessWorks();
	return _statusFlag;
}

void SelfTest::PerformMemoryAnalysis()
{
	const int SIZE_TO_TEST = 1024 * 64;
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

void SelfTest::ValidateExecutionStack()
{
	VariableDynamicStack st(10);
	Variable a(10, VariableKind::Int32);
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
	ASSERT(*ptr2 == *ptr, "Unable to read 8-byte values from 4-byte aligned addresses")
}

