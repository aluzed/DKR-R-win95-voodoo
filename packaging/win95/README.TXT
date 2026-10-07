DIDDY KONG RACING FOR WINDOWS 95 AND 3DFX VOODOO
================================================

An unofficial port. It runs the game's own code, recompiled for the PC, and
draws it with a 3dfx Voodoo card. It contains no part of the game: you need
your own copy of the cartridge's ROM.


WHAT YOU NEED
-------------

  - Windows 95 (or 98).
  - A 3dfx Voodoo graphics card - a Voodoo 2 is what the port was built for -
    with its Windows 95 driver installed. The driver provides GLIDE2X.DLL in
    the WINDOWS\SYSTEM folder; the game tells you if it cannot find it.
  - A sound card with a Windows 95 driver.
  - 64 MB of memory. The game also runs with 32 MB, with about 5 MB to
    spare.
  - A Pentium II at 400 MHz is the target. The figures below were measured on
    an emulation of that machine, not yet on a real one.
  - Your own Diddy Kong Racing ROM: the USA cartridge, version 1.0, as a .z64,
    .v64 or .n64 file. The later "Rev A" (v1.1) and other regions are not
    supported by this build.


INSTALLING
----------

Run SETUP.EXE, from the CD (it starts by itself when the disc goes in) or
from the folder you copied the package into. It installs the game into
C:\GAMES\DKR or a folder you choose, takes your ROM -- put it beside
SETUP.EXE beforehand, or show it where it is when asked -- and puts the game
in Start > Programs. Running it again over an installation updates the game
and keeps DKRR.INI and your saves.

Or by hand:

  1. Copy DKRR.EXE and the .TXT files into a folder of their own, for
     example C:\GAMES\DKR.
  2. Copy your ROM into that same folder.
  3. Start DKRR.EXE.

The first time, the game finds the ROM beside it, checks it, and writes its
path into DKRR.INI. If it cannot find a ROM, or does not accept the one it
finds, a message says why and what to do.

The ROM may also be anywhere else: start the game as DKRR.EXE C:\PATH\DKR.Z64,
or write the path in DKRR.INI:

    [Paths]
    Rom=C:\PATH\DKR.Z64

To remove the game, delete its folder and its shortcut in the Start menu's
Programs; nothing else was written.


CONTROLS
--------

The keyboard, by position (shown here for a US layout; on a French keyboard
the stick is Z Q S D, Z is W and L is A):

    W A S D        the control stick
    Arrow keys     the D-pad
    Space          A          (accelerate)
    Shift          B          (brake)
    Z              Z          (fire an item)
    Enter          Start
    Q / E          L / R
    I J K L        the C buttons

Player two, on the numeric keypad (NumLock on or off):

    8 4 2 6        the control stick (5 is down as well)
    0              A          (accelerate)
    .              B          (brake)
    -              Z          (fire an item)
    +              Start
    7 / 9          L / R

Player two joins by pressing 0 at PLAYER SELECT.

Player one's keys can be changed in DKRR.INI with KEYS=, for example
KEYS=SU:UP,SD:DOWN,SL:LEFT,SR:RIGHT for the stick on the arrows; CONFIG.TXT
lists every control and key name.

Gamepads and joysticks: through Windows 95's own joystick support (Control
Panel > Joystick, where they are also calibrated). The first is player one,
alongside the keyboard; a second is player two. A four-button pad maps its
buttons 1 to 4 to A, B, Z and Start; DKRR.INI can change that, and the dead
zone (CONFIG.TXT, JOY_BUTTONS and JOY_DEADZONE). This has been tested only
without a pad connected: please report how yours behaves.


LEAVING THE GAME
----------------

Alt+F4 quits. Alt+Tab pauses the game and hands the screen back to Windows;
Alt+Tab back to it and it carries on where it was. Shutting Windows down
while the game is paused quits it first. If a quit ever takes longer than
fifteen seconds, the game ends itself and notes it in its log.


SETTINGS
--------

DKRR.INI, beside the game, is written the first time it starts, with every
useful setting commented out and explained. Remove the ';' in front of a line
to use it. CONFIG.TXT describes them all.

The ones a player may want:

    RDRAM_SNAPSHOT=copy     the original port's slower way of drawing
    GLIDE_SWAP=immediate    no wait for the monitor's retrace (may tear)
    OSD=1                   frame rate and timings drawn over the game


YOUR SAVES
----------

The game's saves, the cartridge's and the Controller Pak's, are kept in the
DKR-RUNTIME-DATA folder beside the game, in SAVES and CONTROLLER-PAK-1.MPK.
Copy that folder to keep them. Each save is written with a backup copy, so a
power cut while saving does not lose the previous one.


WHAT TO EXPECT
--------------

On the emulated Pentium II 400 and Voodoo 2:

  - races run at the game's own 30 frames a second nine frames in ten;
  - the adventure's hub world runs at 30 up to its busiest views, where it
    falls to about 25;
  - the sound plays on an emulated Sound Blaster 16 without breaking up in a
    race; it can stutter briefly while a level loads;
  - the screen stays black for about five seconds after the game starts,
    while the 3dfx card is brought up; the very first start takes two or
    three seconds more, to check the ROM;
  - entering a race takes about a second;
  - the 3dfx card shows the game at 640 x 480 and 60 Hz, which every monitor
    of the time can display: two screen refreshes for each of the game's 30
    frames a second, so motion stays even. If a 3dfx control panel forces
    another refresh rate, set it back to 60 Hz.


KNOWN DIFFERENCES FROM THE CONSOLE
----------------------------------

The Voodoo is not the Nintendo 64's graphics chip, and some differences cannot
be removed:

  - textures are filtered slightly differently (the console blends three
    texels, the Voodoo four);
  - the console's edge anti-aliasing is not reproduced;
  - a few of the console's colour combinations are approximated;
  - distant textures may shimmer more;
  - colours are stored with 16 bits and rounded down, which can show as
    banding on long, smooth gradients;
  - surfaces almost touching each other may sort differently; the tracks
    were not seen to have any.


IF SOMETHING GOES WRONG
-----------------------

The game writes what it does in DKR-RUNTIME-DATA\LOGS\RUNTIME.LOG. That file
is what to send with a report.


LICENCES
--------

LICENSE.TXT   this port's own code (MIT)
COPYING.TXT   the GNU General Public License v3, which covers the runtime the
              game is built on (N64ModernRuntime), and therefore DKRR.EXE
THIRDPTY.TXT  the other components and their notices

Diddy Kong Racing is a trademark of its owners. This port is not affiliated
with, nor endorsed by, them, and distributes no part of the game.
