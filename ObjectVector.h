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

		vector()
		{
			_data = nullptr;
			_size = 0;
			_count = 0;
		}
		
		vector(int initialSize, int initialCount)
		{
			Firmata.sendString(F("ABC "), initialSize * sizeof(T));
			if (initialSize < initialCount)
			{
				initialSize = initialCount;
			}
			Firmata.sendStringf(F("ABC 2 %ld"), 4, (int32_t)initialSize * sizeof(T));
			if (initialSize > 0)
			{
				Firmata.sendString(F("ABC 3a "), initialSize * sizeof(T));
				_data = (T*)malloc(initialSize * sizeof(T));
				Firmata.sendString(F("ABC 3b "), initialSize * sizeof(T));
			}
			else
			{
				Firmata.sendString(F("ABC 3c "), initialSize * sizeof(T));
				_data = nullptr;
			}
			Firmata.sendString(F("ABC 4a "));
			Firmata.sendString(F("ABC 4a "));
			Firmata.sendString(F("ABC 4a "));
			Firmata.sendString(F("ABC 4a "));
			Firmata.sendString(F("ABC 4a "));
			_size = initialSize;
			Firmata.sendString(F("ABC 4ac "));
			_count = initialCount;
			Firmata.sendString(F("ABC 4 "));
			Firmata.sendString(F("ABC 4b "));
			Firmata.sendString(F("ABC 4 c"));
			Firmata.sendString(F("ABC 4 d"));
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
				}
				else
				{
					_size *= 2;
					_data = (T*)realloc(_data, _size * sizeof(T));
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
					_size = 10;
					_data = (T*)malloc(_size * sizeof(T));
				}
				else
				{
					_size *= 2;
					_data = (T*)realloc(_data, _size * sizeof(T));
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
	};

}
