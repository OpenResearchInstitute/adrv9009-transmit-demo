# adrv9009-transmit-demo
Demonstration of transmitting samples via IIO with the ADRV9009 on the ADRV9009-EVAL
board.

This is adapted from pluto-transmit-demo, which was based on the standard IIO demo code
from Analog Devices. The receive functions are commented out.

The ADRV9009 api doesn't seem to include ways to configure sample rate interpolation.
This needs to be done by loading a profile. For now, this is done outside of the
executable iio-tx.

In order to make this demo generate some output that could be evaluated visually
on a spectrum analyzer, I replaced the default transmit samples (all zeroes, of
all things!) with a tone that's swept sinusoidally from -20 kHz to +20 kHz.

Here's what that looks like:

https://user-images.githubusercontent.com/5356541/223925492-e374c4c6-6d37-42ef-9d8d-9d4153d3f8e3.mov

