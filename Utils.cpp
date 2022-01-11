// 
// 
//

#include <ConfigurableFirmata.h>
#include "Utils.h"

#ifndef _MSC_VER
/// <summary>
/// Mimics the behavior of strncpy_s, but without the error handling
/// </summary>
/// <param name="strDest">Destination</param>
/// <param name="numberOfElements">Size of target buffer</param>
/// <param name="strSource">Source</param>
/// <param name="count">Maximum number of chars to copy (</param>
/// <returns>An error code</returns>
errno_t strncpy_s(char* strDest, size_t numberOfElements, const char* strSource, size_t count)
{
	memset(strDest, 0, numberOfElements);
	numberOfElements--; // always leave the last element 0
	size_t maxLen = strlen(strSource);
	if (count < maxLen)
	{
		maxLen = count;
	}

	if (numberOfElements < maxLen)
	{
		maxLen = numberOfElements;
	}

	for (size_t i = 0; i < maxLen; i++)
	{
		strDest[i] = strSource[i];
	}

	return 0;
}

errno_t memcpy_s(void* dest, size_t destinationLength, void* source, size_t sourceLength)
{
	memcpy(dest, source, sourceLength);
	return 0;
}

#endif
