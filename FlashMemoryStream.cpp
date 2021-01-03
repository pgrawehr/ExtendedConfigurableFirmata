#include <Stream.h>
#include "FlashMemoryStream.h"

int FlashMemoryStream::read()
{
	if (_pos >= _buffer_size)
	{
		return -1;
	}
	
	return pgm_read_byte(_buffer + (_pos++));
}

int FlashMemoryStream::peek()
  {
	  if (_pos >= _buffer_size)
	  {
		  return -1;
	  }
	  
	  return pgm_read_byte(_buffer + _pos);
  }

