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
        
        // We assume the user has installed Shank using the Setup Wizard
        // which puts the 'shank' command in their global PATH.
        let runCmd = `shank run "${filePath}"`;

        // Create or show the terminal
        let terminal = vscode.window.terminals.find(t => t.name === 'Shank Output');
        if (!terminal) {
            terminal = vscode.window.createTerminal({
                name: 'Shank Output',
                cwd: cwd
            });
        }
        
        terminal.show(true);
        terminal.sendText(runCmd);
    });

    context.subscriptions.push(runCommand);
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
