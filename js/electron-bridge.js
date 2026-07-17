/* =============================================================================
 * electron-bridge.js — интеграция с Electron.
 *   В обычном браузере window.electronAPI отсутствует и модуль ничего не делает.
 *   В Electron: нативные диалоги сохранения/открытия и команды меню.
 * =========================================================================== */
(function () {
  "use strict";
  const API = window.electronAPI;
  if (!API || !API.isElectron) return; // веб-версия — выходим, поведение не меняем

  const AD = window.AD;

  function toast(msg, warn) {
    if (AD && AD.app && AD.app.toast) AD.app.toast(msg, warn);
  }

  function ready(fn) {
    if (document.readyState === "loading") {
      window.addEventListener("DOMContentLoaded", fn);
    } else {
      fn();
    }
  }

  ready(() => {
    const S = AD.store;
    document.body.classList.add("is-electron"); // включает кнопки управления окном в хедере

    // 1) Экспорт JSON → нативный диалог сохранения
    AD.app.exportFile = async function () {
      const name = (S.model.meta.name || "diagram").replace(/\s+/g, "_");
      try {
        const res = await API.saveJSON(name, S.exportJSON());
        if (res && res.ok) toast("Сохранено: " + res.path);
      } catch (e) {
        toast("Ошибка сохранения: " + e.message, true);
      }
    };

    // 2) Открытие → нативный диалог (переопределяем метод, который вызывает меню)
    AD.app.openProject = async function () {
      try {
        const res = await API.openJSON();
        if (res && res.ok) {
          S.importJSON(res.content);
          AD.diagram.render();
          AD.diagram.fitView();
          AD.engine.seek(0);
          AD.app.setProjName();
          toast("Открыто: " + res.name);
        }
      } catch (e) {
        toast("Ошибка открытия: " + e.message, true);
      }
    };

    // 3) Сохранение GIF/WebM → нативный диалог (вместо загрузки в браузере)
    if (AD.exportMedia) {
      AD.exportMedia._download = async function (blob, ext) {
        try {
          const bytes = new Uint8Array(await blob.arrayBuffer());
          const name = (S.model.meta.name || "diagram").replace(/\s+/g, "_");
          const res = await API.saveBinary(name, ext, bytes);
          if (res && res.ok) toast("Сохранено: " + res.path);
        } catch (e) {
          toast("Ошибка сохранения: " + e.message, true);
        }
      };
    }
  });
})();
