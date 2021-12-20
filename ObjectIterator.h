#pragma once

namespace stdSimple
{
template<class T1, class T2>
    struct pair
	{
		pair()
		{
		}
		pair(T1 a, T2 b)
		{
			first = a;
			second = b;
		}
		T1 first;
		T2 second;
	};
}
