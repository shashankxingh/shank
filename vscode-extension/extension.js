const vscode = require('vscode');
const path = require('path');

/**
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
    let runCommand = vscode.commands.registerCommand('shank.runFile', function (uri) {
        // If activated from the title button, it might pass a URI. If not, get the active editor.
        let editor = vscode.window.activeTextEditor;
        let filePath = "";

        if (uri && uri.fsPath) {
            filePath = uri.fsPath;
        } else if (editor) {
            filePath = editor.document.fileName;
        } else {
            vscode.window.showErrorMessage("No Shank file is currently active to run.");
            return;
        }

        if (!filePath.endsWith('.sk')) {
            vscode.window.showErrorMessage("This is not a Shank (.sk) file.");
            return;
        }

        // Get the workspace folder
        let workspaceFolder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filePath));
        let cwd = workspaceFolder ? workspaceFolder.uri.fsPath : path.dirname(filePath);
        
        // Ensure shankc.exe is run. If it's in the workspace root, try to use it.
        // For simplicity, we assume shankc.exe is either in PATH or in the workspace root.
        let compilerPath = '.\\shankc.exe'; 
        
        // Handle path quoting for powershell
        let safeFilePath = `"${filePath}"`;

        // Create or show the terminal
        let terminal = vscode.window.terminals.find(t => t.name === 'Shank Output');
        if (!terminal) {
            terminal = vscode.window.createTerminal({
                name: 'Shank Output',
                cwd: cwd
            });
        }
        
        terminal.show(true);
        terminal.sendText(`clear`);
        terminal.sendText(`${compilerPath} run ${safeFilePath}`);
    });

    context.subscriptions.push(runCommand);
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
