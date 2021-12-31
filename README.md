# uWFG-Pico
Inspired by a description of an [Arbitrary Waveform Generator](https://www.instructables.com/Arbitrary-Wave-Generator-With-the-Raspberry-Pi-Pic/) I decided to try this myself. The [uSDR-Pico](https://github.com/ArjanteMarvelde/uSDR-pico) project focused on getting to know the Pico, especially the multi-core feature, this project will focus on using PIO and DMA. 

![Prototype](doc/Proto.jpg) 

The prototype provides two channels with 8 bit resolution, on which waveforms (sample files) can be played independently. The maximum sample frequency is the system clock, so on a Pico this is by default 125MHz. This obviously sets a limit to the maximum waveform frequency, in the sense that it will be a tradeoff with desired accuracy. If a sample file defining a single wave has a length of 25 samples, the maximum frequency is 125/25 = 5MHz. 

Still, the performance is quite remarkable, and mostly limited by the implementation of the R-2R ladder network on the digital output. Stray capacitance together with the used resistor values will act as a low-pass filter, in case of the prototype maxing out to about 10MHz. 

![1MHz](doc/SQ-1MHz.jpg)  

Above image shows 1MHz square waves on both output channels. It must be noted that the R value in the ladder networks is 0.5k in channel A and 1k in channel B, nicely showing the low-pass effect. 


