BATMAN BEGINS ON PC

The original Xbox game, recompiled to run directly on Windows. It uses the US ISO of
the game, which is already in the assets folder. There is nothing to install.

The whole campaign has been played to the end with this version, at 60 FPS. Defects
can still show up.


HOW TO START

Double-click BatmanBegins.exe. The logos take about 45 seconds, less if you skip
them, and then the PRESS START screen appears.

To quit, use Alt+F4. Before quitting, pause the game and use Save / Quit, then Save
Last Checkpoint. The save goes back to the last checkpoint, not to the exact spot
where you are. Do not close the game while its save message is on screen ("Saving",
then "Game saved").


CONTROLS

A controller connected to the PC works without any setup. The keyboard always works
as player 1, with or without a controller.

The game shows the Xbox controller's buttons. On the keyboard, the defaults are:

  Move: W, A, S, D                Camera: I, J, K, L
  A (jump): Space                 B (action: doors and objects): C
  X (punch): E                    Y (kick): Q
  LT: Left Shift                  RT: Left Ctrl
  Start: Enter                    Back: Backspace
  D-pad: arrow keys

The keys can be changed on the options screen.


OPTIONS SCREEN

Open and close it with Esc on the keyboard, or Back + Start on the controller. While
it is open the game stands still: picture, sound and clock. It has the display mode,
window size, internal resolution, aspect ratio, texture filter, vsync, frame rate and
the keys.


SKIPPING VIDEOS

Hold A on the controller, or Space on the keyboard, for 1 second during a video.
Scenes played in the game's engine cannot be skipped. This is on purpose.


PICTURE AND SPEED

In play the picture runs at 30 FPS. That is the game's own rate, not a defect.

FRAME RATE, on the options screen, offers 60. It is experimental: the whole game has
been played that way, but some parts run too fast, such as the helicopter's flight,
and it may be unstable. A change takes effect the next time the game starts.

At 16:9 the menus and the HUD are 33% wider, as in the original game on a console
connected to a widescreen TV. To see them in their 4:3 shape, choose 4:3 in ASPECT
RATIO on the options screen. The picture then gets black bars at the sides.


WHERE THINGS ARE

Everything stays inside this folder. To move the game somewhere else, move the whole
folder.

  hdd\E\UDATA     The saves
  logs            Logs of the last 5 sessions. batman.log is the latest,
                  batman.1.log the one before, and so on up to batman.4.log
  CrashDumps      A new file each time the game closes because of an error
  settings.toml   The choices made on the options screen
  shaders.cache   Effects already prepared, so the game does not stutter the
                  first time it shows them. If it is deleted, the game makes
                  a new one
  assets          The game's ISO

If settings.toml has an error, the game starts with the default options and keeps a
copy of it as settings.toml.bad.

To report a problem, send the batman.log of that session. Once the game is opened
again, it becomes batman.1.log. If the game closed by itself, also send the newest
file in CrashDumps.

If the whole PC freezes, the last lines of the log may be lost.
