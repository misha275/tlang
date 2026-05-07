import * as cp from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';

function quoteForDisplay(value: string): string {
  if (/^[A-Za-z0-9_\-./\\:]+$/.test(value)) {
    return value;
  }

  return `"${value.replace(/"/g, '\\"')}"`;
}

function resolveCompilerPath(workspaceRoot: string, configuredPath: string): string {
  if (path.isAbsolute(configuredPath)) {
    return configuredPath;
  }

  const workspaceCandidate = path.join(workspaceRoot, configuredPath);
  if (fs.existsSync(workspaceCandidate)) {
    return workspaceCandidate;
  }

  return configuredPath;
}

function resolveOutputDirectory(documentPath: string): string {
  const config = vscode.workspace.getConfiguration('tlang');
  const configured = config.get<string>('defaultOutputDirectory', '').trim();

  if (configured.length > 0) {
    return path.isAbsolute(configured) ? configured : path.join(path.dirname(documentPath), configured);
  }

  return path.dirname(documentPath);
}

function runCompiler(
  compilerPath: string,
  args: string[],
  cwd: string,
  output: vscode.OutputChannel
): Promise<number> {
  return new Promise<number>((resolve) => {
    const child = cp.execFile(compilerPath, args, { cwd, windowsHide: true }, (error, stdout, stderr) => {
      if (stdout.length > 0) {
        output.append(stdout);
      }

      if (stderr.length > 0) {
        output.append(stderr);
      }

      if (error) {
        output.appendLine(String(error.message));
      }
    });

    child.on('exit', (code) => {
      resolve(code ?? 1);
    });

    child.on('error', () => {
      resolve(1);
    });
  });
}

async function compileActiveFile(output: vscode.OutputChannel): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showErrorMessage('TLang: no active editor is open.');
    return;
  }

  const document = editor.document;
  if (document.isUntitled) {
    vscode.window.showErrorMessage('TLang: save the file before compiling.');
    return;
  }

  if (document.languageId !== 'tlang' && path.extname(document.fileName).toLowerCase() !== '.tlang') {
    vscode.window.showErrorMessage('TLang: the active file is not a .tlang document.');
    return;
  }

  const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
  const workspaceRoot = workspaceFolder?.uri.fsPath ?? path.dirname(document.fileName);
  const config = vscode.workspace.getConfiguration('tlang');
  const compilerSetting = config.get<string>('compilerPath', 'tlang.exe');
  const compilerPath = resolveCompilerPath(workspaceRoot, compilerSetting);
  const outputDirectory = resolveOutputDirectory(document.fileName);
  const baseName = path.basename(document.fileName, path.extname(document.fileName));
  const cppPath = path.join(outputDirectory, `${baseName}.cpp`);
  const exePath = path.join(outputDirectory, `${baseName}.exe`);
  const cppOnly = config.get<boolean>('emitCppByDefault', false);

  fs.mkdirSync(outputDirectory, { recursive: true });

  const args = ['compile', document.fileName, '--emit-cpp', cppPath];
  if (!cppOnly) {
    args.push('-o', exePath);
  } else {
    args.push('--cpp-only');
  }

  output.clear();
  output.show(true);
  output.appendLine(`TLang compiler: ${quoteForDisplay(compilerPath)}`);
  output.appendLine(`Working directory: ${quoteForDisplay(workspaceRoot)}`);
  output.appendLine(`Source: ${quoteForDisplay(document.fileName)}`);
  output.appendLine(`Output C++: ${quoteForDisplay(cppPath)}`);
  if (!cppOnly) {
    output.appendLine(`Output executable: ${quoteForDisplay(exePath)}`);
  }
  output.appendLine('');

  const exitCode = await runCompiler(compilerPath, args, workspaceRoot, output);

  if (exitCode === 0) {
    vscode.window.showInformationMessage(`TLang: compiled ${path.basename(document.fileName)} successfully.`);
    return;
  }

  vscode.window.showErrorMessage(`TLang: compiler exited with code ${exitCode}.`);
}

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel('TLang');
  context.subscriptions.push(output);
  context.subscriptions.push(
    vscode.commands.registerCommand('tlang.compileActiveFile', () => compileActiveFile(output))
  );
}

export function deactivate(): void {
  return undefined;
}
