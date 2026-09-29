// The preload bridge: a narrow, typed surface between the renderer and the
// main process (project folder, files, the compiler, the terminal).

const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("studio", {
    openProject: () => ipcRenderer.invoke("project:open"),
    listProject: () => ipcRenderer.invoke("project:list"),
    readFile: (name) => ipcRenderer.invoke("file:read", name),
    writeFile: (name, content) => ipcRenderer.invoke("file:write", name, content),
    newFile: (name) => ipcRenderer.invoke("file:new", name),
    deleteFile: (name) => ipcRenderer.invoke("file:delete", name),

    compile: (entry) => ipcRenderer.invoke("compile", entry),

    startTerminal: (cwd) => ipcRenderer.invoke("terminal:start", cwd),
    stopTerminal: () => ipcRenderer.invoke("terminal:stop"),
    sendTerminalData: (data) => ipcRenderer.send("terminal:data", data),
    resizeTerminal: (cols, rows) => ipcRenderer.send("terminal:resize", cols, rows),
    onTerminalData: (callback) => ipcRenderer.on("terminal:data", (_event, data) => callback(data)),
    onTerminalExit: (callback) => ipcRenderer.on("terminal:exit", () => callback()),
});
