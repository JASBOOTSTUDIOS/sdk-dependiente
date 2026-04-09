'use strict';

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

function fileExists(targetPath) {
    try {
        return fs.existsSync(targetPath);
    } catch {
        return false;
    }
}

function dirExists(targetPath) {
    try {
        return fs.existsSync(targetPath) && fs.statSync(targetPath).isDirectory();
    } catch {
        return false;
    }
}

function resolveProjectRoot(filePath) {
    let current = path.dirname(filePath);
    while (true) {
        if (
            fileExists(path.join(current, '.vscode', 'run-jasb.cjs')) ||
            dirExists(path.join(current, 'stdlib')) ||
            dirExists(path.join(current, 'sdk-dependiente'))
        ) {
            return current;
        }
        const parent = path.dirname(current);
        if (parent === current) break;
        current = parent;
    }

    const workspaceFolder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filePath));
    return workspaceFolder ? workspaceFolder.uri.fsPath : path.dirname(filePath);
}

function findBundledLauncher(projectRoot) {
    const launcher = path.join(projectRoot, '.vscode', 'run-jasb.cjs');
    return fileExists(launcher) ? launcher : null;
}

function findJbc(projectRoot) {
    const rels = [
        ['sdk-dependiente', 'jas-compiler-c', 'bin', 'jbc.exe'],
        ['sdk-dependiente', 'jas-compiler-c', 'bin', 'jbc-next.exe'],
        ['sdk', 'jas-compiler-c', 'bin', 'jbc.exe'],
        ['sdk', 'jas-compiler-c', 'bin', 'jbc'],
        ['sdk-dependiente', 'jas-compiler-c', 'bin', 'jbc'],
        ['..', 'bin', 'jbc.exe'],
        ['..', 'bin', 'jbc_new.exe'],
        ['..', 'bin', 'jbc-next.exe'],
        ['..', 'bin', 'jbc']
    ];

    for (const parts of rels) {
        const candidate = path.join(projectRoot, ...parts);
        if (fileExists(candidate)) return candidate;
    }
    return null;
}

function quotePowerShell(value) {
    return `'${String(value).replace(/'/g, "''")}'`;
}

function quoteShell(value) {
    return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function buildCommand({ launcherPath, jbcPath, filePath }) {
    if (process.platform === 'win32') {
        if (launcherPath) {
            return `node ${quotePowerShell(launcherPath)} ${quotePowerShell(filePath)}`;
        }
        return `${quotePowerShell(jbcPath)} ${quotePowerShell(filePath)} -e`;
    }

    if (launcherPath) {
        return `node ${quoteShell(launcherPath)} ${quoteShell(filePath)}`;
    }
    return `${quoteShell(jbcPath)} ${quoteShell(filePath)} -e`;
}

function getTerminal() {
    const terminalName = 'Jasboot';
    let terminal = vscode.window.terminals.find((item) => item.name === terminalName);
    if (!terminal) {
        terminal = vscode.window.createTerminal({ name: terminalName });
    }
    return terminal;
}

async function runActiveFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showErrorMessage('No hay un editor activo para ejecutar.');
        return;
    }

    const filePath = editor.document.uri.fsPath;
    const ext = path.extname(filePath).toLowerCase();
    if (ext !== '.jasb' && ext !== '.jd') {
        vscode.window.showErrorMessage('Abre un archivo .jasb o .jd para ejecutar con Jasboot.');
        return;
    }

    if (editor.document.isDirty) {
        await editor.document.save();
    }

    const projectRoot = resolveProjectRoot(filePath);
    const launcherPath = findBundledLauncher(projectRoot);
    const jbcPath = findJbc(projectRoot);

    if (!launcherPath && !jbcPath) {
        vscode.window.showErrorMessage(
            'No se encontro un launcher Jasboot ni un compilador jbc para ejecutar el archivo activo.'
        );
        return;
    }

    if (ext === '.jd' && !launcherPath) {
        vscode.window.showErrorMessage(
            'Para ejecutar archivos .jd se necesita un launcher compatible como .vscode/run-jasb.cjs en el proyecto.'
        );
        return;
    }

    const command = buildCommand({ launcherPath, jbcPath, filePath });
    const terminal = getTerminal();
    terminal.show(true);
    terminal.sendText(command, true);
}

function activate(context) {
    context.subscriptions.push(vscode.commands.registerCommand('jasboot.runActiveFile', runActiveFile));
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
