# uWFG-Pico
Inspired by a description of an [Arbitrary Waveform Generator](https://www.instructables.com/Arbitrary-Wave-Generator-With-the-Raspberry-Pi-Pic/) I decided to try this myself. The [uSDR-Pico](https://github.com/ArjanteMarvelde/uSDR-pico) project focused on getting to know the Pico, especially the multi-core feature, this project will focus on using PIO and DMA. 
![doc/Proto.jpg](Prototype) 
The prototype provides two channels with 8 bit resolution, on which waveforms (sample files) can be played independently. The maximum sample frequency is the system clock, so on a Pico this is by default 125MHz. 


