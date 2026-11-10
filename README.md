# mac-doom

A macOS port of the classic DOOM C source code. The game engine and software renderer remain in C; AppKit provides the window, keyboard, and mouse input. Sound effects are output through CoreAudio, and MUS-format music is played through Apple's DLS synthesizer. The mouse is captured while the game window is focused. Audio output requires macOS.

## Port changes

- A dedicated macOS build target using Apple Clang; Linux/X11 and OSS audio dependencies are not used for this target.
- AppKit video and input backend with the Doom framebuffer, keyboard, mouse capture.
- CoreAudio backend for Doom sound effects and MUS music playback through Apple's DLS synthesizer.
- 64-bit/arm64 fixes for memory sizes, WAD data structures, and pointer alignment.
- Object files are kept under `build/macos/obj/`. The build copies `wad/doom.wad` next to the executable.

## Requirements

- macOS with the Xcode Command Line Tools (Apple Clang and `make`). Install them if needed:

  ```sh
  xcode-select --install
  ```

- A valid Doom IWAD at `wad/doom.wad` is needed. The repository does not include a WAD for local testing.

## Build and run

From the repository root:

```sh
cd mac-doom
make macos
cd build/macos
./doom
```

The executable and copied `doom.wad` will be in `mac-doom/build/macos/`. To remove the macOS build files:

```sh
cd mac-doom
make clean-macos
```

The build has been verified on Apple Silicon with Apple Clang, tested using the DOOM shareware v1.10.

## Notes

For now, this remains the classic Doom port with software rendering; it does not (yet) use a Metal renderer.


## Known Issues

- Music/sound are buggy and require further investigation.
- Exiting the game via the "Quit Game" option from within the game triggers a "trap trace," which macOS reports as "doom quit unexpectedly."
- The mouse is trapped within the window and cannot be easily released—the only way to free it is by closing the program.


## Possible future plans

- Convert the renderer to metal, higher resolution and perhaps also a higher frame rate.
- a modern control scheme using mouse and keyboard, comparable to that of a modern FPS
- potential support for classic mods that do not require a Doom source port (e.g. UZDoom)
- a small game launcher for mod organization and settings (keyboard, graphics, etc...)