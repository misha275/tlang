# TLang

TLang is a word-only compiled language that lowers to C++20.

## Build the compiler

```powershell
g++ -std=c++20 -O2 .\tlang.cpp -o .\tlang.exe
```

## Compile a program

```powershell
.\tlang.exe compile .\examples\example.tlang -o .\examples\example.exe
```

The compiler performs lexical validation, parsing, semantic checks, and native executable generation through `g++`.

Internally TLang still lowers to C++20, but the intermediate file is temporary by default. Use `--emit-cpp FILE` to keep the generated C++ or `--cpp-only` to stop after emitting C++.

## Libraries

TLang programs use `.tlang`. Libraries use the separate `.tlib` extension and live in `libraries/`.

```text
USE LIBRARY PIXELGRAPHICS END LINE
```

The compiler resolves that directive to `libraries/pixelgraphics.tlib` and compiles it together with the program.

## Windows API interface primitives

TLang has built-in WinAPI wrappers that can be used as a base for UI libraries:

- `WINMESSAGE TITLE MESSAGE` returns `NUMBER`
- `WINWINDOW TITLE WIDTH HEIGHT` returns a window handle as `NUMBER`
- `WINBUTTON PARENT TEXT X Y WIDTH HEIGHT ID` returns a control handle
- `WINLABEL PARENT TEXT X Y WIDTH HEIGHT` returns a control handle
- `WINEDIT PARENT TEXT X Y WIDTH HEIGHT ID` returns a control handle
- `WINTEXT HANDLE` returns `TEXT`
- `WINSETTEXT HANDLE TEXT` returns `VOID`
- `WINSHOW HANDLE` returns `VOID`
- `WINWAIT` returns `BOOL` and waits for one Windows message
- `WINRUN` returns `NUMBER` and runs the blocking message loop
- `WINCOMMAND` returns the last control command id, or `ZERO`
- `WINQUIT` returns `VOID`

When any `WIN...` function is used, the generated C++ links `user32` and `gdi32` automatically. Add `--windows-gui` to build without a console window:

```powershell
.\tlang.exe compile .\examples\windows_ui.tlang --windows-gui -o .\examples\windows_ui.exe
```

## Pixel graphics library

`libraries/pixelgraphics.tlib` is a small graphics layer where every visible pixel is stored as a coordinate-to-color dictionary entry inside the runtime. A program writes pixels through:

- `GRAPHICSWINDOW TITLE WIDTH HEIGHT`
- `GRAPHICSCOLOR RED GREEN BLUE`
- `GRAPHICSHEX HEX`
- `GRAPHICSPIXEL CANVAS X Y COLOR`
- `GRAPHICSGET CANVAS X Y`
- `GRAPHICSCLEAR CANVAS`
- `GRAPHICSSHOW CANVAS`
- `GRAPHICSWAIT`
- `GRAPHICSRUN`

Hex colors are text values. These forms are accepted: `#FF0033`, `FF0033`, `0XFF0033`, and short `F03`.

Example:

```powershell
.\tlang.exe compile .\examples\pixel_dictionary.tlang --windows-gui -o .\examples\pixel_dictionary.exe
```
