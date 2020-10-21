#ifndef OBJECTSTACK_H
#define OBJECTSTACK_H
#include "Arduino.h"

class ObjectStack
{
	// TODO: Replace int with void*
	int _maxSize;
	int _stackPtr = 0;
	uint32_t* data;
	public:
	ObjectStack(int maxSize)
	{
		_stackPtr = maxSize - 1;
		_maxSize = maxSize;
		data = (uint32_t*)malloc(_maxSize * sizeof(uint32_t));
	}
	
	~ObjectStack()
	{
		free(data);
		data = NULL;
	}
	
	void push(uint32_t object)
	{
		data[_stackPtr] = object;
		_stackPtr--;
	}
	
	uint32_t pop()
	{
		_stackPtr ++;
		return data[_stackPtr];
	}
	
	uint32_t peek()
	{
		return data[_stackPtr + 1];
	}
	
	bool empty()
	{
		return _stackPtr == _maxSize - 1;
	}
	
};
#endif