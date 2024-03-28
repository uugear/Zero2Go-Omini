# Zero2Go-Omini

Zero2Go Omini is a Raspberry Pi Zero sized (or pHAT shape) extension board that can work as wide input range power supply for Raspberry Pi. It supports all Raspberry Pi models that has the 40-pin GPIO header, including A+, B+, 2B, 3B, 3B+, 4B, Zero and Zero W.

Zero2Go Omini has quite wide input range (3.5V~28V), so it is convenient to power your Raspberry Pi with power bank, Li-Po battery pack, solar panel, car battery or different kinds of power adapters etc. You can also configure it as a UPS.

This repository contains the source code of firmware and software for Zero2Go Omini.

The firmware is compiled and uploaded to Zero2Go Omini via Arduino IDE (with ATTinyCore installed).

The software is written in BASH.

More information about the firmware and software can be found in the user manual: http://www.uugear.com/doc/Zero2Go_Omini_UserManual.pdf

# Firmware build instructions

You can use the provided Dockerfile and Makefile (made with [Arduino-Makefile](https://github.com/sudar/Arduino-Makefile/)) to quickly set up a build environment with all the required dependencies.:

```
cd Firmware/Zero2Go-Omini
docker build -t zero2go-omini .
```

The firmware can then be built with:

```
cd Firmware/Zero2Go-Omini
docker run -ti -v $(pwd):/src zero2go-omini:latest
```
