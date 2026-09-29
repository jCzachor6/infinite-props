// The Electron main process: the window, the project-folder operations, the
// compiler bridge (the native ipc binary) and the embedded terminal
// (node-pty), where the user runs CLI agents (opencode, codex, copilot)
// against the real project files.

const { app, BrowserWindow, dialog, ipcMain } = require("electron");
const fs = require("fs");
const path = require("path");
const { execFile, spawn } = require("child_process");

let win = null;
let projectDir = null;
let ptyProcess = null;

// The repository root, used to locate the native ipc binary (build/tools/ipc).
// Override with the IPROP_IPC environment variable for a packaged build.
function repoRoot() {
    return path.resolve(__dirname, "..", "..");
}

function ipcBinary() {
    return process.env.IPROP_IPC ||
        path.join(repoRoot(), "build", "tools", "ipc");
}

function listIpsFiles() {
    if (!projectDir) return [];
    return fs.readdirSync(projectDir)
        .filter((name) => name.endsWith(".ips"))
        .sort();
}

function safePath(name) {
    const resolved = path.resolve(projectDir, name);
    if (!resolved.startsWith(path.resolve(projectDir))) {
        throw new Error("path escapes the project folder");
    }
    return resolved;
}

function createWindow() {
    win = new BrowserWindow({
        width: 1280,
        height: 840,
        title: "iprop studio",
        backgroundColor: "#10161d",
        webPreferences: {
            preload: path.join(__dirname, "preload.cjs"),
            contextIsolation: true,
            nodeIntegration: false,
        },
    });
    win.loadFile(path.join(__dirname, "..", "dist", "index.html"));
}

// ---- project and files -----------------------------------------------------

ipcMain.handle("project:open", async () => {
    const result = await dialog.showOpenDialog(win, {
        title: "Open a project folder",
        properties: ["openDirectory"],
    });
    if (result.canceled || result.filePaths.length === 0) {
        return null;
    }
    projectDir = result.filePaths[0];
    return { dir: projectDir, files: listIpsFiles() };
});

ipcMain.handle("project:list", async () => {
    return { dir: projectDir, files: projectDir ? listIpsFiles() : [] };
});

ipcMain.handle("file:read", async (_event, name) => {
    return fs.readFileSync(safePath(name), "utf8");
});

ipcMain.handle("file:write", async (_event, name, content) => {
    fs.writeFileSync(safePath(name), content, "utf8");
    return true;
});

ipcMain.handle("file:new", async (_event, name) => {
    const target = safePath(name.endsWith(".ips") ? name : name + ".ips");
    fs.writeFileSync(target, "asset untitled {\n  size 64x64\n  origin 32,56\n\n  draw(v, a) {\n  }\n}\n", { flag: "wx" });
    return listIpsFiles();
});

ipcMain.handle("file:delete", async (_event, name) => {
    fs.unlinkSync(safePath(name));
    return listIpsFiles();
});

// ---- the compiler bridge -----------------------------------------------------

ipcMain.handle("compile", async (_event, entry) => {
    const binary = ipcBinary();
    return new Promise((resolve) => {
        execFile(binary, [entry], { cwd: projectDir, timeout: 15000 },
            (error, stdout, stderr) => {
                resolve({ code: error ? (error.code ?? 1) : 0, stdout, stderr });
            });
    });
});

// ---- the embedded terminal -----------------------------------------------------

ipcMain.handle("terminal:start", async (_event, cwd) => {
    if (ptyProcess) {
        ptyProcess.kill();
        ptyProcess = null;
    }
    const shell = process.env.SHELL || "/bin/zsh";
    ptyProcess = require("node-pty").spawn(shell, [], {
        name: "xterm-256color",
        cols: 100,
        rows: 24,
        cwd: cwd || projectDir || process.env.HOME,
        env: process.env,
    });
    ptyProcess.onData((data) => {
        if (win) win.webContents.send("terminal:data", data);
    });
    ptyProcess.onExit(() => {
        ptyProcess = null;
        if (win) win.webContents.send("terminal:exit");
    });
    return true;
});

ipcMain.on("terminal:data", (_event, data) => {
    if (ptyProcess) ptyProcess.write(data);
});

ipcMain.on("terminal:resize", (_event, cols, rows) => {
    if (ptyProcess) ptyProcess.resize(cols, rows);
});

ipcMain.handle("terminal:stop", async () => {
    if (ptyProcess) {
        ptyProcess.kill();
        ptyProcess = null;
    }
    return true;
});

// ---- app lifecycle -------------------------------------------------------------

app.whenReady().then(() => {
    createWindow();
    app.on("activate", () => {
        if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
});

app.on("window-all-closed", () => {
    if (process.platform !== "darwin") app.quit();
});
