# mac-doom

A macOS port of the classic DOOM C source code. The game engine and software renderer remain in C; AppKit provides the window, keyboard, and mouse input. Sound effects are output through CoreAudio, and MUS-format music is played through Apple's DLS synthesizer. The mouse is captured while the game window is focused. Audio output requires macOS.

## Port changes

- A dedicated macOS build target using Apple Clang; Linux/X11 and OSS audio dependencies are not used for this target.
- AppKit video and input backend with the Doom framebuffer, keyboard, mouse capture, and clean window closing.
- CoreAudio backend for Doom sound effects and MUS music playback through Apple's DLS synthesizer.
- 64-bit/arm64 fixes for memory sizes, WAD data structures, and pointer alignment.
- Object files are kept under `build/macos/obj/`. The build copies `wad/doom1.wad` next to the executable.

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

The build has been verified on Apple Silicon with Apple Clang.
Music/sound still needs to be revised and can be buggy!

## Notes

This remains the classic software-rendered Doom port; it does not use a Metal renderer.


## Possible future plans

Convert the renderer to metal, higher resolution and perhaps also a higher frame rate.