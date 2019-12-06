# Linux terminal KIM-1 Emulator

This is an emulator for the MOS KIM-1 6502 computer.

The emulator runs from the command-line, and generally works like
the KIM-1 where you key in an address or data and then tell it to run.
Single-step appears to work, as do the timers. The Wumpus game, which
uses timers and displays messages on the LEDs seems to work. The
program goes into raw mode when reading keys, so you don't have to
hit enter, unless you are entering the filename and load address
when loading a .bin file.

The scrolling of the Wumpus messages is slow. I have assumed that the
6530 timer ticks are 1MHz, and that may be correct, I see that the
Wumpus game description tells you how to speed it up.

It supports the same keys as the KIM-UNO emulator's serial interface,
but adds the ability to load a binary file into memory. Unlike the
KIM-UNO, this emulator does not yet have built-in ROM programs.
Here's a shot of the wumpus game in action - the command-line interface
is a little ugly, but it works.
![Image of Wumpus game](https://github.com/wutka/kim1-emulator/blob/master/img/wumpus.png)

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
The display mimics the KIM-1 display, which has a set of 4 7-segment LED
displays that show the current address, and 2 that show the data value
at that address. To change the current address, if you are in address
mode, just enter the digits. The previous digits scroll to the left, so
if the display shows 1234 and you enter 8 it will now read 2348.

To enter data into a location, switch to data mode and enter the 2-digit
hex value.

To run a program, set the address to the beginning of the program and
hit ctrl-g for GO.

## Emulation Info
I have tried as much as possible to let the original KIM-1 ROM do all
the work. There are two areas where I had to cheat a little.

First, since I can only read characters and not capture keydown/keyup
events, I need a way to signal that a key is no longer pressed. I could
have just put a timer on it, but instead, I check the program counter
of the 6502 and if it hits 1f79 or 1f90 (the two RTS instructions from
the GETKEY routine) it assumes the key has been captured by the ROM
software and clears the pending key.

Second, the KIM-1 ROM strobes the LEDs rapidly. If you were emulating
this in hardware, you could probably just pass the LED strobes to
physical LEDs. The difficulty with trying to capture and interpret
the LED strobes is that before it writes the next LED, it clears the
previous one. Since I am trying to print out the LED as a character,
I can't easily tell what the correct value for an LED is. So,
I check the PC and when it gets to 1f56 in the ROM, I know that the
accumulator has the value for the LED specified by the X register.
Then I skip over the delay routine just for efficiency.

### Keyboard scanning
I had a terrible time getting the keyboard scanning to work, I'm
mainly writing this section in case someone else is trying to figure
it out.

To read the keyboard, the KIM-1 ROM scans each of three rows of keys
to see if any have been pressed. There are 7 possible keys in each
row, and it is wired such that when a key is pressed, the SAD
register on the 6530 has a 0 in that bit position. When a key is
not pressed, bits 0-6 of the SAD will all be 1. Bit 7 of the SAD
indicates whether or not there is a bit waiting from the serial
port. Since I am not emulating the serial port right now, I keep this
bit at 1, meaning there is no incoming serial data.

The keys for each row are:

    0  1  2  3  4  5  6
    7  8  9  a  b  c  d
    e  f  AD DA +  GO PC

The RS and ST keys are wired to the RES and IRQ pins on the 6502, so
they do not have keycodes in the ROM. Numbering from left to right,
top to bottom, the key codes for the keys are 0-9, a-f then AD=0x10,
DA=0x11 ... PC = 0x14. A key code of 0x15 means no key has been
pressed.

When the ROM scans the keyboard, it first sets the accumulator to 0xff
then sets SBD to 1 to indicate the first row, then reads the keys. 
It uses the BIT instruction to AND the value in SAD with the
accumulator. Since a pressed key is represented by a 0 bit, if there
are no keys pressed, the accumulator will still be 0xff after this,
otherwise there will be a 0 at one bit position to indicate the
key pressed. It then sets SBD to 3 to scan for the second row, and then
to 5 to scan the third row.

The ROM also does a scan to see if there is serial data by setting
SBD to 7. If there is no serial data, SAD should be 0xff (set all
the key bits to 1, and bit 7 is 1 to indicate no serial data).

