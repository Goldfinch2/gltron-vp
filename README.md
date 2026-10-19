# GLtron for VectorPie

[GLtron](http://www.gltron.org), the light-cycle game by Andreas Umbach,
running on a vector monitor through the [VectorPie SDK](https://github.com/Goldfinch2/VectorPie).

GLtron's own game logic (cycles, trails, collisions, booster, wall buster,
the computer players) runs unchanged. What GLtron did with OpenGL, Lua and
SDL is done here with the SDK: the trails are solid walls that hide what is
behind them, the cycles are drawn as solid objects, and the controls are the
cabinet's (advmame.rc).

## Playing

* **COIN**: a credit. **START 1 / START 2**: a one or two player game, for
  one or two credits (the button's lamp blinks on the title screen when
  there are enough); computer players fill the
  arena (four cycles in all). A round ends when one cycle is left, or as soon
  as every human player has crashed. Rounds follow one another; the scores
  add up (a point for every cycle that crashes while you are still riding).
* **Lives**: 5 each. Crashing in a round costs one; a player out of lives
  sits out the rest of the game, and when every player is out it is GAME
  OVER. A score in the top 10 is entered with three initials (stick up/down
  for the letter, button 1 next, button 2 back) and saved in
  `save/highscores.txt`, next to the game (the format the VectorPie menu
  reads for its high-score display). The title screen alternates with the
  high-score table.
* **Left / right**: turn (each press turns 90 degrees).
* **Button 1**: booster (while held, uses energy). **Button 2**: wall buster
  (pass through walls, uses energy). Energy refills over time.
* **The exit button**: back to the VectorPie menu.
* **CONTROLS**: on the title screen, TAB (or START 1 and START 2 together)
  opens the controls page. Each player's TURN LEFT, TURN RIGHT, BOOST and
  WALL BUSTER can be laid out on any key, button, hat or stick: START 1 on a
  row, then the control (more on the same row add to it); START 2 sets none,
  DEL the standard (the cabinet's controls). Kept in `save/controls.ini`.

One player: the camera follows your cycle. Two players: one view of the whole
arena. Before a game, the computer plays a demo.

`leds.cfg` gives cabinets with LED-lit controls the game's lamp colors and
its control guide (hold the select button on the game in the menu): stick =
STEER, button 1 = BOOSTER, button 2 = WALL BUSTER, both players. "booster"
and "wall buster" are not among the cabinet's shared voice clips: add
`guide/booster.wav` and `guide/wall_buster.wav` to have them spoken (the
labels show either way).

## Building

In WSL2 Debian, with the VectorPie SDK installed (in the SDK: `cmake --install build-pc` and
`cmake --install build-pi`, to `~/VectorPie/dist/`; again after changing it) (its
`tools/setup-wsl.sh` sets everything up):

```sh
cmake --preset pc && cmake --build --preset pc     # build-pc/gltron.exe
cmake --preset pi && cmake --build --preset pi     # build-pi/gltron
```

Run `build-pc/gltron.exe --windowed` on the PC. On the cabinet, copy
`build-pi/gltron` and `build-pi/data/` side by side, or install the game
package:

```sh
cmake --build --preset pi --target vpk             # build-pi/gltron-<version>.vpk
```

Put the `.vpk` in a USB drive's `games/` folder, or copy it to the cabinet's
`/persistent/games/packages/`, then on the cabinet: Settings -> GAMES ->
INSTALL GAMES. `game.ini` is the package's manifest;
`marquee.png` goes in the package: the menu shows it, and so does the HDMI
screen while the game plays on the vector monitor.

## What changed from GLtron

* `src/game/event.c`, `engine.c`, `computer.c`, `computer_utilities.c` and
  `src/base/` are GLtron's, with their includes reduced to one header and the
  calls into GLtron's video, Lua and console code removed. Two fixes: a random
  start direction could be out of range (`&` instead of `%`), and with no
  human player the computer players' spawn set was read uninitialized, and a
  computer player's last-turn time was never reset between rounds (with a
  64-bit `long`, as on the Pi, it could stop turning).
* `src/game/settings.c` holds GLtron's `scripts/config.lua` rules as a C
  table; `src/game/level.c` is `levels/square.lua` as C.
* `src/main.c` (game flow, controls, sound) and `src/render.c` (drawing) are
  new, written for the SDK.
* The arena walls carry GLtron's wall texture (`art/classic/walls.png`)
  as vector lines (`src/wall_art.h`), edited over the texture with
  `tools/wall_editor.py` (Python 3 with Tk and Pillow; `wall_editor.bat` on
  Windows).
* The music, Peter Hajba's 'Revenge of Cats' (GLtron's
  `music/song_revenge_of_cats.it`), is converted to Ogg Vorbis
  (`data/music/revenge_of_cats.ogg`, played by the SDK as it is) and looped. GLtron ships it with a
  copyright notice but no license terms (see `CREDITS`).
* Not ported: the other textures, the skybox, reflections and menus.

## License

GPL version 2 or later, like GLtron: see `COPYING`. The models and sounds in
`data/` are GLtron's; their authors are in `CREDITS`.
