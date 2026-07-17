/* =============================================================================
 * main.js — главный процесс Electron: окно, меню, нативные файловые диалоги
 * =========================================================================== */
"use strict";
const { app, BrowserWindow, Menu, dialog, ipcMain, shell } = require("electron");
const path = require("path");
const fs = require("fs/promises");

const isDev = process.argv.includes("--dev");
let mainWindow = null;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 1024,
    minHeight: 680,
    backgroundColor: "#0b1220",
    title: "Animated Diagrams",
    frame: false, // без системной рамки — используем кастомный хедер приложения
    show: false, // покажем после ready-to-show, чтобы не мигало белым
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      spellcheck: false,
    },
  });

  mainWindow.loadFile(path.join(__dirname, "..", "index.html"));

  mainWindow.once("ready-to-show", () => {
    mainWindow.show();
    if (isDev) mainWindow.webContents.openDevTools({ mode: "detach" });
  });

  // сообщаем рендереру о смене состояния «развёрнуто», чтобы обновить иконку кнопки
  mainWindow.on("maximize", () => mainWindow.webContents.send("win:maximized", true));
  mainWindow.on("unmaximize", () => mainWindow.webContents.send("win:maximized", false));

  // внешние ссылки открываем в системном браузере, а не в окне приложения
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:/.test(url)) {
      shell.openExternal(url);
      return { action: "deny" };
    }
    return { action: "allow" };
  });

  mainWindow.on("closed", () => {
    mainWindow = null;
  });
}

/* --- управление окном (кастомный хедер вместо системной рамки) --- */
ipcMain.on("win:minimize", () => mainWindow && mainWindow.minimize());
ipcMain.on("win:toggleMaximize", () => {
  if (!mainWindow) return;
  mainWindow.isMaximized() ? mainWindow.unmaximize() : mainWindow.maximize();
});
ipcMain.on("win:close", () => mainWindow && mainWindow.close());
ipcMain.on("win:toggleDevTools", () => mainWindow && mainWindow.webContents.toggleDevTools());

/* --- IPC: нативное сохранение JSON --- */
ipcMain.handle("dialog:saveJSON", async (_e, defaultName, content) => {
  const { canceled, filePath } = await dialog.showSaveDialog(mainWindow, {
    title: "Сохранить диаграмму",
    defaultPath: (defaultName || "diagram") + ".json",
    filters: [{ name: "JSON", extensions: ["json"] }],
  });
  if (canceled || !filePath) return { ok: false, canceled: true };
  await fs.writeFile(filePath, content, "utf8");
  return { ok: true, path: filePath };
});

/* --- IPC: нативное открытие JSON --- */
ipcMain.handle("dialog:openJSON", async () => {
  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    title: "Открыть диаграмму",
    filters: [{ name: "JSON", extensions: ["json"] }],
    properties: ["openFile"],
  });
  if (canceled || !filePaths.length) return { ok: false, canceled: true };
  const content = await fs.readFile(filePaths[0], "utf8");
  return { ok: true, name: path.basename(filePaths[0]), content };
});

/* --- IPC: сохранить бинарник (GIF/WebM) на диск --- */
ipcMain.handle("dialog:saveBinary", async (_e, defaultName, ext, bytes) => {
  const { canceled, filePath } = await dialog.showSaveDialog(mainWindow, {
    title: "Сохранить файл",
    defaultPath: (defaultName || "animation") + "." + ext,
    filters: [{ name: ext.toUpperCase(), extensions: [ext] }],
  });
  if (canceled || !filePath) return { ok: false, canceled: true };
  await fs.writeFile(filePath, Buffer.from(bytes));
  return { ok: true, path: filePath };
});

app.whenReady().then(() => {
  Menu.setApplicationMenu(null); // системное меню отключено — используем кастомный хедер
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
