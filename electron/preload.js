/* =============================================================================
 * preload.js — безопасный мост между рендерером и главным процессом.
 *   contextIsolation: рендерер получает только явно выставленный API,
 *   без доступа к Node/ipcRenderer напрямую.
 * =========================================================================== */
"use strict";
const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("electronAPI", {
  isElectron: true,
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node,
  },
  /** нативное сохранение JSON; content — строка */
  saveJSON: (defaultName, content) => ipcRenderer.invoke("dialog:saveJSON", defaultName, content),
  /** нативное открытие JSON → { ok, name, content } */
  openJSON: () => ipcRenderer.invoke("dialog:openJSON"),
  /** сохранить бинарный файл (GIF/WebM); bytes — Uint8Array/массив байт */
  saveBinary: (defaultName, ext, bytes) => ipcRenderer.invoke("dialog:saveBinary", defaultName, ext, bytes),
  /** переключить инструменты разработчика */
  toggleDevTools: () => ipcRenderer.send("win:toggleDevTools"),
  /** управление окном для кастомного хедера (система без рамки) */
  windowControls: {
    minimize: () => ipcRenderer.send("win:minimize"),
    toggleMaximize: () => ipcRenderer.send("win:toggleMaximize"),
    close: () => ipcRenderer.send("win:close"),
    /** подписка на смену состояния «развёрнуто» → cb(isMaximized) */
    onMaximized: (cb) => {
      const handler = (_e, m) => cb(m);
      ipcRenderer.on("win:maximized", handler);
      return () => ipcRenderer.removeListener("win:maximized", handler);
    },
  },
});
