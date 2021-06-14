# ExtendedConfigurableFirmata
Extension to ConfigurableFirmata, providing a C# interpreter for running managed code on Arduinos and similar microcontroller boards.

# Supported boards:
- Arduino Due

# To build and run:
- Install Arduino IDE
- Install Arduino Due board support package
- Install supported version of ConfigurableFirmata (github.com/pgrawehr/ConfigurableFirmata)
- Patch low-level memory management functions (see https://arduino.stackexchange.com/questions/80535/memory-allocation-on-arduino-due-never-returns-null)
- Install Adafruit DHT library
- Enable Exception support (https://arduino.stackexchange.com/questions/80634/how-to-enable-exception-handling-on-the-arduino-due/80635#80635)
