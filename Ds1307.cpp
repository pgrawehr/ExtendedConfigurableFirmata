#include <ConfigurableFirmata.h>
#include "Ds1307.h"
#include "Wire.h"

void Ds1307::Init()
{
	Wire.begin();
}


int64_t Ds1307::ReadTime()
{
	short year;
	byte month;
	byte day;
	byte hour;
	byte minute;
	byte second;

	Wire.beginTransmission((byte)DS1307_ADDRESS);
	Wire.write(0);       // set register address
	if (Wire.endTransmission() != 0)
		return _previousValue;

	byte data[7];
	// Must read the whole register set at once, to ensure consistency
	Wire.requestFrom(DS1307_ADDRESS, 7);
	for (uint8_t i = 0; i < 7; i++)
	{
		if (!Wire.available())
		{
			return _previousValue;
		}
		data[i] = (byte)Wire.read();
	}
	year = 2000 + BcdToDecimal(data[6]);
	month = BcdToDecimal(data[5]);
	day = BcdToDecimal(data[4]);
	// Register 3 is the day of the week, which we don't need, since the .NET DateTime class can do that (and can do it right)
	if (data[2] & 0x40)
	{
		// 12 hour bit set
		hour = BcdToDecimal(data[2] & 0x1F);
		if (data[2] & 0x20)
		{
			// PM
			hour += 12;
		}
	}
	else
	{
		hour = BcdToDecimal(data[2]); // Since bit 6 is 0, we can just do a direct conversion
	}
	minute = BcdToDecimal(data[1]);
	second = BcdToDecimal(data[0] & 0x7F); // Mask the CH bit (should be 0, though, or the clock is disabled, anyway)
	_previousValue = GetAsDateTimeTicks(year, month, day, hour, minute, second, 0);
	// _previousValue = GetAsDateTimeTicks(2021, 2, 28, hour, minute, second, 0); // This was a sunday
	return _previousValue;
}
