#include "ConfigurableFirmata.h"
#include "SelfTest.h"
#include "Variable.h"
#include "VariableContainer.h"
#include "VariableDynamicStack.h"

bool SelfTest::PerformSelfTest()
{
	_statusFlag = true;
	PerformMemoryAnalysis();
	ValidateExecutionStack();
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
	if (b.Int32 != 10)
	{
		Firmata.sendString(F("Internal selftest error: 1"));
		_statusFlag = false;
		return;
	}
	st.pop();
	if (!st.empty())
	{
		Firmata.sendString(F("Internal selftest error: 2"));
		_statusFlag = false;
		return;
	}

	b.Int32 = 0xFFFF;
	st.push(b);
	Variable c = st.top();
	st.pop();
	if (b.Int32 != c.Int32)
	{
		Firmata.sendString(F("Internal selftest error: 3"));
		_statusFlag = false;
		return;
	}
	st.push(b);
	st.pop();
	st.push(c);
	if (b.Int32 != 10)
	{
		Firmata.sendString(F("Internal selftest error: Stack overwrites instances"));
		_statusFlag = false;
		return;
	}

	st.pop();
	st.push(a);
	st.push(b);
	c = st.nth(1);
	if (c.Int32 != a.Int32)
	{
		Firmata.sendString(F("Internal selftest error: Stack count doesn't fit"));
		_statusFlag = false;
		return;
	}
}

void SelfTest::UnallignedAccessWorks()
{
	int64_t* ptr = (int64_t*)malloc(20);
	*ptr = -1;
	int64_t* ptr2 = ptr;
	ASSERT(*ptr2 == *ptr, F("Pointer access error"));
}

