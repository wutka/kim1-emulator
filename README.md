# Linux terminal KIM-1 Emulator

This is an emulator for the MOS KIM-1 6502 computer.

The emulator runs from the command-line, and generally works like
the KIM-1 where you key in an address or data and then tell it to run.
Some features like single-stepping may not work yet, and the Wumpus
program dies unexpectedly.

It supports the same keys as the KIM-UNO emulator's serial interface,
but adds the ability to load a binary file into memory. Unlike the
KIM-UNO, this emulator does not yet have built-in ROM programs.

## Keyboard commands

  ctrl-a    - switch to address mode
  ctrl-d    - switch to data mode
  0-9, a-f  - enter a hex digit either into the address or data window
  ctrl-p    - display program counter
  ctrl-t    - step
  ctrl-r    - reset
  ctrl-g    - go (execute)
  ctrl-[    - enter single-step mode
  ctrl-]    - exit single-step mode
  +         - go to the next memory location
  l         - load a program, you are prompted for the filename and load address

## Display
The display mimics the KIM-1 display, which has a set of 4 7-segment LED displays that
show the current address, and 2 that show the data value at that address. To change the
current address, if you are in address mode, just enter the digits. The previous digits
scroll to the left, so if the display shows 1234 and you enter 8 it will now read 2348.

To enter data into a location, switch to data mode and enter the 2-digit hex value.

To run a program, set the address to the beginning of the program and hit ctrl-g for GO.
