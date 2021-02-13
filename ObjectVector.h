#pragma once

#include "Exceptions.h"

namespace stdSimple
{
	/// <summary>
	/// This is a generic iterator interface.
	/// </summary>
	template<class T>
	class complexIteratorBase
	{
	public:
		virtual ~complexIteratorBase()
		{
		}
		
		virtual T* Current() = 0;
		
		virtual bool Next() = 0;

		virtual void Reset() = 0;
	};
	
	
	template<class T, class TSize = size_t>
	class vector
	{
	private:
		TSize _size;
		TSize _count;
		T* _data;

	public:
		class complexVectorIterator : public complexIteratorBase<T>
		{
			vector<T, TSize>* _list;
			int _currentIndex;

			// This ctor is only used for housekeeping purposes
		public:
			complexVectorIterator()
			{
				_list = nullptr;
				_currentIndex = -1;
			}
			
			complexVectorIterator(vector<T, TSize>* list)
			{
				_list = list;
				_currentIndex = -1; // before first
			}

			T* Current() override
			{
				return &_list->at(_currentIndex);
			}

			bool Next() override
			{
				return ++_currentIndex < (int)_list->size();
			}

			void Reset() override
			{
				_currentIndex = -1;
			}
		};
	
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

		/// <summary>
		/// Releases excess memory that was previously reserved
		/// </summary>
		void truncate()
		{
			if (_count != 0)
			{
				_size = _count;
				_data = (T*)realloc(_data, _size * sizeof(T));
			}
			else if (_data != nullptr)
			{
				_size = 0;
				free(_data);
				_data = nullptr;
			}
		}

		void pop_back()
		{
			--_count;
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

		void clear(bool truncate = false)
		{
			if (_data != nullptr)
			{
				for (size_t i = 0; i < _count; i++)
				{
					_data[i].~T();
				}
			}

			if (truncate)
			{
				_size = 0;
				free(_data);
				_data = nullptr;
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

		iterator begin() const
		{
			return _data;
		}

		iterator end() const
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

		complexVectorIterator GetIterator() const
		{
			return complexVectorIterator((vector<T, TSize>*)this);
		}
	};

}
