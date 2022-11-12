#pragma once

#include "Exceptions.h"
#include "MemoryManagement.h"

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
	
	
	template<class T, class TSize = size_t, size_t vectorIncrement = 10>
	class vector
	{
	private:
		TSize _size;
		TSize _count;
		T* _data;
		bool _isReadOnly;

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
			_isReadOnly = false;
			verifyIntegrity();
		}
		
		vector(int initialSize, int initialCount)
		{
			if (initialSize < initialCount)
			{
				initialSize = initialCount;
			}
			if (initialSize > 0)
			{
				_data = (T*)mallocEx(initialSize * sizeof(T));
				if (_data == nullptr)
				{
					throw OutOfMemoryException::Throw("Out of memory initializing vector");
				}
			}
			else
			{
				_data = nullptr;
			}
			_size = initialSize;
			_count = initialCount;
			verifyIntegrity();
		}

		~vector()
		{
			if (_data != nullptr && !_isReadOnly)
			{
				freeEx(_data);
			}
			_data = nullptr;
			_count = 0;
			_size = 0;
			verifyIntegrity();
		}

		/// <summary>
		/// Adds an element to the end of the list
		/// </summary>
		/// <param name="object">The object to insert. It will be copied</param>
		/// <returns>The 0-based index of the new element</returns>
		int push_back(T& object)
		{
			if (_isReadOnly)
			{
				throw Exception("Cannot modify read-only vector");
			}
			if (_count < _size)
			{
				_data[_count++] = object;
			}
			else
			{
				if (_size == 0)
				{
					_size = vectorIncrement;
					_data = (T*)mallocEx(_size * sizeof(T));
					if (_data == nullptr)
					{
						OutOfMemoryException::Throw("Out of memory pushing first element of vector");
						return -1;
					}
				}
				else
				{
					_size += vectorIncrement;
					// Need a temp variable here, so that in case of an error, the original block is still available
					T* temp = (T*)reallocEx(_data, _size * sizeof(T));
					if (temp == nullptr)
					{
						Firmata.sendStringf(F("Bad: No more memory for %d elements"), _size);
						OutOfMemoryException::Throw("Out of memory resizing vector");
						return -1;
					}
					_data = temp;
				}
				
				_data[_count++] = object;
			}
			return _count - 1;
		}

		void push_back(const T& object)
		{
			if (_isReadOnly)
			{
				throw Exception("Cannot modify read-only vector");
			}
			if (_count < _size)
			{
				_data[_count++] = object;
			}
			else
			{
				if (_size == 0)
				{
					_size = vectorIncrement;
					_data = (T*)mallocEx(_size * sizeof(T));
					if (_data == nullptr)
					{
						OutOfMemoryException::Throw("Out of memory pushing first element of vector");
						return;
					}
				}
				else
				{
					_size += vectorIncrement;
					T* temp = (T*)reallocEx(_data, _size * sizeof(T));
					if (temp == nullptr)
					{
						Firmata.sendStringf(F("Extra Bad: No more memory for %d elements"), _size);
						OutOfMemoryException::Throw("Out of memory increasing vector size");
						return;
					}
					_data = temp;
				}
				
				_data[_count++] = object;
			}
		}

		/// <summary>
		/// Releases excess memory that was previously reserved
		/// </summary>
		void truncate()
		{
			if (_isReadOnly)
			{
				_isReadOnly = false;
				_data = nullptr;
				_count = 0;
				_size = 0;
			}
			if (_count != 0)
			{
				_size = _count;
				T* temp = (T*)reallocEx(_data, _size * sizeof(T));
				if (temp == nullptr)
				{
					OutOfMemoryException::Throw("Out of memory truncating vector");
				}
				_data = temp;
			}
			else if (_data != nullptr)
			{
				_size = 0;
				freeEx(_data);
				_data = nullptr;
			}
			verifyIntegrity();
		}

		void initFrom(TSize size, T data)
		{
			if (_isReadOnly)
			{
				_data = nullptr;
			}
			if (_data != nullptr)
			{
				freeEx(_data);
				_data = nullptr;
			}

			_size = 0;
			_count = 0;

			if (size > 0)
			{
				_isReadOnly = true;
				_data = (T*)data;
				_size = size;
				_count = size;
				if (_data == nullptr)
				{
					throw ExecutionEngineException("Init from empty data");
				}
			}
		}

		void verifyIntegrity()
		{
			if (_data == nullptr && _size != 0)
			{
				throw new ExecutionEngineException("Inconsistent data container");
			}
		}

		void pop_back()
		{
			if (_isReadOnly)
			{
				throw Exception("Cannot modify read-only vector");
			}
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

		inline bool empty() const
		{
			return _count == 0;
		}

		inline size_t size() const
		{
			return _count;
		}

		void clear(bool truncate = false)
		{
			if (_isReadOnly)
			{
				_isReadOnly = false;
				_data = nullptr;
				_size = 0; // Because we cannot use it again in this state.
			}

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
				freeEx(_data);
				_data = nullptr;
			}
			
			_count = 0;
			verifyIntegrity();
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

		void remove(int index)
		{
			if (_isReadOnly)
			{
				throw Exception("Cannot modify read-only vector");
			}
			int elementsToMove = _count - (index + 1); // May be 0
			memmove(&_data[index], &_data[index + 1], elementsToMove * sizeof(T));
			--_count;
		}
	};

}
