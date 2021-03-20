#pragma once

#include "ObjectVector.h"
#include "ObjectIterator.h"

namespace stdSimple
{
	// A very simple unordered dictionary.
	template <class TKey, class TValue>
	class map
	{
	private:
		vector<TKey> _keys;
		vector<TValue> _values;
	public:
		class iterator
		{
		private:
			int _index;
			vector<TKey>& _keys;
			vector<TValue>& _values;
		public:

			iterator(int startIndex, vector<TKey>& keys, vector<TValue>& values)
			: _index(startIndex), _keys(keys), _values(values)
			{
			}
			
			bool operator!=(const iterator& m) const {
				return m._index != _index;
			}

			bool operator==(const iterator& m) const {
				return m._index == _index;
			}

			iterator& operator++()
			{
				++_index;
				return *this;
			}

			TKey& first()
			{
				return _keys[_index];
			}

			TValue& second()
			{
				return _values[_index];
			}
		};
		
		map()
		{
		}

		vector<TValue>& values()
		{
			return _values;
		}

		TValue& at(const TKey &key)
		{
			for (size_t i = 0; i < _keys.size(); i++)
			{
				if (_keys[i] == key)
				{
					return _values[i];
				}
			}

			// Should we really enable exceptions?
			// throw exception(F("Element not found"));
			// This is a hack. It generates a null-reference
			return *(TValue*)nullptr;
		}

		bool contains(const TKey &key)
		{
			for (size_t i = 0; i < _keys.size(); i++)
			{
				if (_keys[i] == key)
				{
					return true;
				}
			}

			return false;
		}

		void insert(const TKey &key, const TValue &value)
		{
			_keys.push_back(key);
			_values.push_back(value);
		}

		void clear(bool truncate = false)
		{
			_keys.clear(truncate);
			_values.clear(truncate);
		}

		size_t size()
		{
			return _keys.size();
		}

		iterator begin()
		{
			return iterator(0, _keys, _values);
		}

		iterator end()
		{
			return iterator(_keys.size(), _keys, _values);
		}
	};
}
