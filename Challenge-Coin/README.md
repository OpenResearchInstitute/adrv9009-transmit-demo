# Challenge Coin

This 2022.2 versioned Vitis project transmits tones for a single channel Opulent Voice demonstration on the zc706 + adrv9009.

## Vitis IDE GitHub Integration

The Vitis integrated development environment has GitHub integration. The basics are described in this AMD article: https://support.xilinx.com/s/article/1173362?language=en_US

## Introduction

The code is based on the 2022.2 branch example code adrv9009-stream.c. The platform was built from the 2022.2 branch of the ADI HDL reference design, by using the exported .xsa file as outlined in "Working with FPGAs" guide in the Documents repository under Remote Labs. 

## Experiments and Current Status

- [ ]  Establish version control in this GitHub folder.
- [x]  The original example code was built and run. 
- [x]  The transmit-all-zeros were replaced with randomly selected numbers and a carrier observed.
- [ ]  wiggle.c demonstration was attempted. The sampling frequency was updated, but the signals did not match the Pluto demonstration. 
- [x]  Transmit buffer was set up as cyclic and an approximately 16 kHz sine wave was constructed and loaded up. 
