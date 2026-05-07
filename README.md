# TLang

TLang is a word-only compiled language that lowers to C++20.

## Build the compiler

```powershell
g++ -std=c++20 -O2 .\tlang.cpp -o .\tlang.exe
```

## Compile a program

```powershell
.\tlang.exe compile .\examples\example.tlang --emit-cpp .\examples\example.cpp -o .\examples\example.exe
```

The compiler performs lexical validation, parsing, semantic checks, C++ emission, and native executable generation through `g++`.

Use `--cpp-only` to stop after emitting C++.
