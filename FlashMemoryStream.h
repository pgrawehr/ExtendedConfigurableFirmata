#pragma once

#include <Stream.h>

/*
 * A FlashMemoryStream can be used to get data from a const PROGMEM array, i.e. to simulate larger communication input when
 * the serial port is used otherwise (i.e. for a debugger)
 */
class FlashMemoryStream : public Stream {
  const uint8_t *_buffer; // Note: This is assumed to point to flash. The Arduino uses a Harvard Architecture!
  uint16_t _buffer_size;
  uint16_t _pos;
public:
  FlashMemoryStream(const uint8_t* buffer, uint16_t buffer_size)
  {
	  _pos = 0;
	 _buffer = buffer;
	 _buffer_size = buffer_size;
  }
  
  virtual size_t write(uint8_t)
  {
	  // This buffer cannot be written to
	  return 0;
  }
  virtual int availableForWrite(void)
  {
	  return 0;
  }
  
  virtual int available()
  {
	  return _buffer_size - _pos;
  }
  
  virtual int read();
  virtual int peek();
  virtual void flush()
  {
  }
};
