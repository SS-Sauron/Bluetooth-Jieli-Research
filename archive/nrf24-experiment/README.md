# NRF24L01 2.4 GHz Experiment (Shelved)

## What was attempted
- Added NRF24L01+PA+LNA module to the scanner ESP32 via VSPI (CE=4, CSN=5, SCK=18, MOSI=23, MISO=19)
- Implemented boot-time module detection via STATUS register
- Added promiscuous spectrum scanning using RPD (Received Power Detector) across 126 channels
- Six-pipe noise-address pattern from RF24 library

## Why it was shelved
- Spectrum scan never detected real 2.4 GHz activity (all dots)
- Multiple iterations of promiscuous setup didn't resolve the issue
- Likely hardware-level problem (SPI bus, CSN strapping pin, or power supply)

## Files preserved
- nrf24_radio.c — SPI driver, module detection, spectrum scan, jammer stubs
- nrf24_radio.h — Public API with compile-time feature toggle
- Kconfig.projbuild — menuconfig integration

## If revisiting
- Try moving CSN from GPIO 5 (strapping pin) to GPIO 16
- Add external pull-up on CSN, pull-down on CE
- Test with Arduino RF24 library first to confirm hardware works
- Verify 3.3V supply can deliver 115 mA peak for PA+LNA module
