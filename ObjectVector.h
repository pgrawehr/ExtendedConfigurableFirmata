#pragma once

#include "Exceptions.h"

namespace stdSimple
{
	template<class T>
	class vector
	{
	private:
		size_t _size;
		size_t _count;
		T* _data;
	public:
		typedef T* iterator;

		vector()
		{
			_data = nullptr;
			_size = 0;
			_count = 0;
		}
		
		vector(int initialSize, int initialCount)
		{
			if (initialSize < initialCount)
			{
				initialSize = initialCount;
			}
			if (initialSize > 0)
			{
				_data = (T*)malloc(initialSize * sizeof(T));
				if (_data == nullptr)
				{
					throw OutOfMemoryException::OutOfMemoryExceptionInstance;
				}
			}
			else
			{
				_data = nullptr;
			}
			_size = initialSize;
			_count = initialCount;
		}

		~vector()
		{
			if (_data != nullptr)
			{
				free(_data);
			}
			_data = nullptr;
			_count = 0;
		}

		void push_back(T& object)
		{
			if (_count < _size)
			{
				_data[_count++] = object;
			}
			else
			{
				if (_size == 0)
				{
					_size = 10;
					_data = (T*)malloc(_size * sizeof(T));
					if (_data == nullptr)
					{
						throw OutOfMemoryException::OutOfMemoryExceptionInstance;
					}
				}
				else
				{
					_size *= 2;
					_data = (T*)realloc(_data, _size * sizeof(T));
					if (_data == nullptr)
					{
						throw OutOfMemoryException::OutOfMemoryExceptionInstance;
					}
				}
				
				_data[_count++] = object;
			}
		}

		void push_back(const T& object)
		{
			if (_count < _size)
			{
				_data[_count++] = object;
			}
			else
			{
				if (_size == 0)
				{
					_size = 4;
					_data = (T*)malloc(_size * sizeof(T));
					if (_data == nullptr)
					{
						throw OutOfMemoryException::OutOfMemoryExceptionInstance;
					}
				}
				else
				{
					_size += 4;
					_data = (T*)realloc(_data, _size * sizeof(T));
					if (_data == nullptr)
					{
						throw OutOfMemoryException::OutOfMemoryExceptionInstance;
					}
				}
				
				_data[_count++] = object;
			}
		}

		void pop_back()
		{
			_count--;
		}

		T& at(size_t index)
		{
			return _data[index];
		}

		T& at(const size_t index) const
		{
			return _data[index];
		}

		T& back()
		{
			return _data[_count - 1];
		}

		bool empty() const
		{
			return _count == 0;
		}

		size_t size() const
		{
			return _count;
		}

		void clear()
		{
			if (_data != nullptr)
			{
				for (size_t i = 0; i < _count; i++)
				{
					_data[i].~T();
				}
			}
			_count = 0;
		}

		T& operator[] (size_t index)
		{
			return at(index);
		}

		T& operator[] (const size_t index) const
		{
			return at(index);
		}

		iterator begin()
		{
			return _data;
		}

		iterator end()
		{
			return _data + _count;
		}

		bool contains(const T& value)
		{
			for (iterator it = begin(); it != end(); ++it)
			{
				if (*it == value)
				{
					return true;
				}
			}
			
			return false;
		}
	};

}
