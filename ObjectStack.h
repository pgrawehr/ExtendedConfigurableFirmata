#pragma once

#include "ObjectVector.h"

namespace stdSimple
{
	template <class T>
	class stack : public vector<T>
	{
	public:
		stack(int initialMemorySize) :
			vector<T>(initialMemorySize, 0)
		{
		}

		inline void push(T object)
		{
			vector<T>::push_back(object);
		}

		inline void pop()
		{
			vector<T>::pop_back();
		}

		T& top()
		{
			return vector<T>::back();
		}

	};
}