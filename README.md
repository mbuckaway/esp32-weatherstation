# ESP32 AWS Weather Station

This is an adaptation of ESP-IDF AWS IT Core code provided by Espressif. I've added code to read a BMP280, One wire temperature sensor, BH1750 light sensor, and a rainsensors.com RH-15 rain sensor. The data is uploaded to AWS IOT core to a topic related to the weather station and the device id of the ESP32 chip. This data, in turn, or stored in a AWS Time series database.

This document is working in progress as is the code.
## Building

Building the code requires running the updatemodules.sh script to pull on the submodules. This application requires my WIFI module, the Espressif AWS module, and UncleRus's ESP-LIB module.

TO BE COMPLETED.
