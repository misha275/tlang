# TLang VS Code Extension

This extension adds basic language support for TLang:

- `.tlang` file association
- built-in custom file icons for `.tlang` and `.tlib`
- syntax highlighting
- snippets for common language constructs
- a command to compile the active TLang file through the existing `tlang.exe` compiler

The extension also contributes a file icon theme. Select `TLang File Icons` from the icon theme picker if you want the same custom TLang and TLib icons enforced through a file icon theme.

## Compile command

Open a `.tlang` file and run `TLang: Compile Active File` from the Command Palette.

The extension looks for the compiler in this order:

1. `tlang.compilerPath` setting
2. `tlang.exe` in the workspace root
3. the configured command on your `PATH`

## Installation

To install the extension manually, you need to package it into a `.vsix` file and install it in VS Code.

### Build the extension

```powershell
npm install
npm run compile
```

After compilation, the extension entry point will be emitted to `out/extension.js`.

### Package the extension

```powershell
npm run package
```

This creates a `tlang-vscode-extension-0.0.3.vsix` file in the current directory.

### Install in VS Code

**Option 1: From the command line**

```powershell
code --install-extension tlang-vscode-extension-0.0.3.vsix
```

**Option 2: From VS Code UI**

1. Open Extensions view (`Ctrl+Shift+X` or `Cmd+Shift+X`)
2. Click the three-dot menu and select **Install from VSIX**
3. Select the `.vsix` file

### Configure the compiler path

If `tlang.exe` is not in your workspace root or on `PATH`, configure the extension in VS Code settings:

```json
{
  "tlang.compilerPath": "/path/to/tlang.exe"
}
```

## Development

For development, use `npm run watch` to recompile on file changes.

```powershell
npm install
npm run watch
```
