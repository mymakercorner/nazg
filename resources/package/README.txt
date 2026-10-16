Nazg @VERSION@ -- test build for Windows
=======================================

Nazg is a keyboard configurator for VIA and Vial keyboards: change your keymap, macros, tap
dances, combos, lighting and the rest, from a native app, without a browser.

This is a TEST BUILD. It is shared so people can try it on their own keyboards before a first
public release. Things will be missing or wrong; please say so (see "Reporting" below).


Running it
----------

Unzip the folder anywhere and run nazg.exe. Nothing is installed.

Windows 10 or 11, 64-bit. The build is not signed yet, so Windows may say "Windows protected
your PC": click "More info", then "Run anyway".

Nazg notices a keyboard being plugged in or unplugged: the keyboard list follows, and an open
keyboard that is unplugged closes.


What it knows about your keyboard
---------------------------------

- A Vial keyboard describes itself: it just works.
- A VIA keyboard needs its definition, the .json file describing its layout. Nazg ships
  VIA's whole official registry (@OFFICIAL@ definitions) and @COMMUNITY@ community ones published by
  vendors for boards VIA lacks. If your board is in neither, Nazg asks for the via.json your
  vendor provides when you open the board ("Import a definition..."); Settings has the same
  button.

Nazg keeps imported definitions and its settings in %APPDATA%\mymakercorner\Nazg\.

Changes are written to the keyboard as you make them, as in VIA and Vial. Your keyboard keeps
them when unplugged.


Reporting
---------

Problems and ideas: https://github.com/mymakercorner/nazg/issues

Please say which keyboard, whether it runs VIA or Vial, and what you did. Settings shows the
version of Nazg and of its definitions under "About" and "Definitions".


Licence
-------

Nazg is free software under the GNU General Public License, version 3 or later: see LICENSE.
Its source is at https://github.com/mymakercorner/nazg -- this build is the tag v@VERSION@.
The libraries and fonts it includes keep their own licences: see the licences folder and
fonts\.
