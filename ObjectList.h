#ifndef OBJECTLIST_H
#define OBJECTLIST_H
#include "Arduino.h"

class ObjectList
{
	// TODO: Replace int with void*
	int _maxSize;
	int _count;
	uint32_t* _data;
	public:
	ObjectList(int maxSize, int initialSize = 0)
	{
		_maxSize = maxSize;
		if (_maxSize > 0)
		{
			_data = (uint32_t*)malloc(_maxSize * sizeof(uint32_t));
		}
		else 
		{
			_data = NULL;
		}
		
		_count = initialSize;
	}
	
	~ObjectList()
	{
		free(_data);
		_data = NULL;
	}
	
	void Add(uint32_t object)
	{
		if (_count < _maxSize)
		{
			_data[_count++] = object;
		}
	}
	
	uint32_t Get(int index)
	{
		return _data[index];
	}
	
	void Set(int index, uint32_t object)
	{
		_data[index] = object;
	}
	
	uint32_t* AddressOf(int index)
	{
		return _data + index;
	}
};

#endif