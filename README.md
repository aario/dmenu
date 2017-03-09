dmenu - dynamic menu
====================
dmenu is an efficient dynamic menu for X.

Main Source
-----------
This is a fork of dmenu - suckless project at:
    http://git.suckless.org/dmenu

Added Features
--------------
* Blur Effect  A fast and nice blur effect. Does not need OpenGL and is all CPU based.
* Meta Key     Meta Key/Super Key closes the menu
* Mouse        Full mouse support
* dmenu_run    Now sorts applications by most executed
* dmenu_win    Displays a list of window titles instead of commands and then switches to selected window
* dmenu_vol    Displays volume percentages from 0 to 100% and sends it to amixer to set 'Master' channel
* dmenu_bl     Displays brightness percentages from 0 to 100% and sends it to xbacklight to set it
* dmenu_media  Sends media player commands! Play/Pause, Previous, Next, etc.
* dmenu_custom Displays of a list of custom scripts located in $HOME/.dwm/custom_scripts
* dmenu_home   Browses through home folder and opens files
* dmenu_apps   Browses through applications menu and launches apps
* dmenu_all    Shows a menu with shortcuts to all above items
* lines        Default value of lines in config.def.h is now set to 10. Originally it was 0.
* Works best with my fork of dwm

![Screenshot](screenshot.png?raw=true "Fully CPU Based No OpenGL Static Blur Effect")

Requirements
------------
In order to build dmenu you need the Xlib header files.
In order to run dmenu_win you need wmctrl utility installed.
In order to run dmenu_vol you need alsa_utils installed.
In order to run dmenu_bl you need xbacklight installed.


Installation
------------
Edit config.mk to match your local setup (dmenu is installed into
the /usr/local namespace by default).

Afterwards enter the following command to build and install dmenu
(if necessary as root):

    make clean install


Running dmenu
-------------
See the man page for details.


## THE STACK-BLUR PART OF THE CODE IS BASED ON GREATE WORK BY:
Mario Klingemann <mario@quasimondo.com>

Link to the original work:
- http://incubator.quasimondo.com/processing/fast_blur_deluxe.php

The original source code of stack-blur:
- http://incubator.quasimondo.com/processing/stackblur.pde
