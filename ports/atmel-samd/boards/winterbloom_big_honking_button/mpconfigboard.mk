# Adafruit
USB_VID = 0x239A
# Allocated for Big Honking Button at https://github.com/adafruit/circuitpython/issues/2715
USB_PID = 0x6005
USB_PRODUCT = "Big Honking Button"
USB_MANUFACTURER = "Winterbloom"

CHIP_VARIANT = SAMD21G18A
CHIP_FAMILY = samd21

SPI_FLASH_FILESYSTEM = 1
EXTERNAL_FLASH_DEVICE_COUNT = 1
EXTERNAL_FLASH_DEVICES = "GD25Q16C"
LONGINT_IMPL = MPZ

CIRCUITPY_AUDIOIO = 1

# Disable modules that are unusable on this special-purpose board.
CIRCUITPY_BITBANGIO = 0
CIRCUITPY_FRAMEBUFFERIO = 0
CIRCUITPY_FREQUENCYIO = 0
CIRCUITPY_AUDIOBUSIO = 0
CIRCUITPY_BLEIO = 0
CIRCUITPY_DISPLAYIO = 0
CIRCUITPY_GAMEPAD = 0
CIRCUITPY_I2CPERIPHERAL = 0
CIRCUITPY_NETWORK = 0
CIRCUITPY_TOUCHIO = 0
CIRCUITPY_RGBMATRIX = 0
CIRCUITPY_PS2IO = 0
CIRCUITPY_USB_HID = 0
CIRCUITPY_USB_MIDI = 0
CIRCUITPY_RTC = 0
