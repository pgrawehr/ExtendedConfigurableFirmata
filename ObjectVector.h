#pragma once

namespace stdSimple
{
	class exception
	{
	private:
		const __FlashStringHelper* _error;
	public:
		exception(const __FlashStringHelper* error)
		{
		}

		const char* what()
		{
			return nullptr; // TODO
		}
	};

	template<class T>
	class vector
	{
	private:
		size_t _size;
		size_t _count;
		T* _data;
	public:
		typedef T* iterator;
		vector(int initialSize = 0, int initialCount = 0)
		{
			if (initialSize < initialCount)
			{
				initialSize = initialCount;
			}
			if (initialSize > 0)
			{
				_data = (T*)malloc(initialSize * sizeof(T));
			}
			else
			{
				_data = NULL;
			}

			_size = initialSize;
			_count = initialCount;
		}

		~vector()
		{
			free(_data);
			_data = NULL;
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
				}
				else
				{
					_size *= 2;
				}
				_data = (T*)realloc(_data, _size * sizeof(T));
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
					_size = 10;
				}
				else
				{
					_size *= 2;
				}
				_data = (T*)realloc(_data, _size * sizeof(T));
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

		bool empty()
		{
			return _count == 0;
		}

		size_t size()
		{
			return _count;
		}

		void clear()
		{
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
			return _data + _count + 1;
		}
	};

}
