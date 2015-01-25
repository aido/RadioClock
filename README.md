RadioClock Library
==================

A noise resilent radio clock signal decoder library for Arduino.

This repository contains a radio clock signal decoder library for Arduino.
It was designed with the goal of creating the most noise resilent
decoder possible.

There are also some ready to run examples.

For details and background information you may want to consult the
DCF77 section of the blog at http://blog.blinkenlight.net/experiments/dcf77/.

The library itself and its examples are documented at
http://blog.blinkenlight.net/experiments/dcf77/dcf77-library/.
This page also contains a short FAQ section. Please read this page
first if you encounter any issues with the library.


Attention: the library requires a crystal oscillator. The Arduino
Uno comes out of the box with a resonator. Thus it will not work
with this library. "Older" designs like the Blinkenlighty
http://www.amazon.de/gp/product/3645651306/?ie=UTF8&camp=1638&creative=6742&linkCode=ur2&site-redirect=de&tag=wwwblinkenlig-21
have a (more expensive) crystal oscillator.

The library currently decodes the DCF77 radio clock signal transmitted from 
Mainflingen, Germany and the MSF radio clock signal transmitted from Anthorn, UK.
