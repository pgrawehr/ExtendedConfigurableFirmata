#ifndef OBJECTSTACK_H
#define OBJECTSTACK_H
#include "Arduino.h"

class ObjectStack
{
	// TODO: Replace int with void*
	int _maxSize;
	int _stackPtr = 0;
	uint32_t* _data;
	public:
	ObjectStack(int maxSize)
	{
		_stackPtr = maxSize - 1;
		_maxSize = maxSize;
		_data = NULL;
		
		if (_maxSize > 0)
		{
			_data = (uint32_t*)malloc(_maxSize * sizeof(uint32_t));
		}
	}
	
	~ObjectStack()
	{
		free(_data);
		_data = NULL;
	}
	
	inline void push(uint32_t object)
	{
		_data[_stackPtr] = object;
		_stackPtr--;
	}
	
	inline uint32_t pop()
	{
		_stackPtr ++;
		return _data[_stackPtr];
	}
	
	inline uint32_t peek()
	{
		return _data[_stackPtr + 1];
	}
	
	bool empty()
	{
		return _stackPtr == _maxSize - 1;
	}
	
};
#endif