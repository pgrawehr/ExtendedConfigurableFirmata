#pragma once

#include "Arduino.h"
#include "ObjectVector.h"

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
		map()
		{
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

		void clear()
		{
			_keys.clear();
			_values.clear();
		}

		size_t size()
		{
			return _keys.size();
		}
	};
}