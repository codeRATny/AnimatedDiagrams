/* =============================================================================
 * menu.js — кастомный хедер-меню в стиле приложения (без системного меню).
 *   Работает и в браузере, и в Electron. Пункты, специфичные для десктопа
 *   (окно, DevTools, выход), показываются только при наличии electronAPI.
 * =========================================================================== */
(function () {
  "use strict";
  const AD = window.AD;
  const API = window.electronAPI;
  const isEl = !!(API && API.isElectron);

  function toggleFullscreen() {
    const d = document;
    if (!d.fullscreenElement) (d.documentElement.requestFullscreen || function () {}).call(d.documentElement);
    else d.exitFullscreen();
  }

  function about() {
    const v = isEl ? API.versions : null;
    alert(
      "Animated Diagrams\n" +
      "Редактор анимированных диаграмм и flow-сценариев." +
      (v ? "\n\nElectron " + v.electron + " · Chromium " + v.chrome + " · Node " + v.node : "")
    );
  }

  /* --- структура меню --- */
  function menus() {
    const A = AD.app;
    const file = [
      { label: "Новая диаграмма", acc: "Ctrl+N", act: () => A.newDiagram() },
      { label: "Загрузить пример", act: () => A.loadSample() },
      { label: "Переименовать…", act: () => A.renameProject() },
      { sep: true },
      { label: "Открыть…", acc: "Ctrl+O", act: () => A.openProject() },
      { label: "Сохранить JSON…", acc: "Ctrl+S", act: () => A.exportFile() },
      { sep: true },
      { label: "Экспорт GIF / Видео…", acc: "Ctrl+E", act: () => AD.exportMedia.open() },
    ];
    if (isEl) file.push({ sep: true }, { label: "Выход", acc: "Ctrl+Q", act: () => API.windowControls.close() });

    const view = [
      { label: "Вписать в экран", act: () => AD.diagram.fitView() },
      { label: "Сбросить масштаб", act: () => AD.diagram.resetView() },
      { label: "Увеличить", acc: "+", act: () => AD.diagram.zoomBy(1.2) },
      { label: "Уменьшить", acc: "−", act: () => AD.diagram.zoomBy(1 / 1.2) },
      { sep: true },
      { label: "Полный экран", acc: "F11", act: toggleFullscreen },
    ];
    if (isEl) view.push(
      { sep: true },
      { label: "Перезагрузить", act: () => location.reload() },
      { label: "Инструменты разработчика", act: () => API.toggleDevTools && API.toggleDevTools() }
    );

    return [
      { label: "Файл", items: file },
      { label: "Правка", items: [{ label: "Удалить выделенное", acc: "Del", act: () => AD.app.deleteSelected() }] },
      { label: "Вид", items: view },
      { label: "Справка", items: [{ label: "О программе", act: about }] },
    ];
  }

  const Menu = (AD.menu = {
    _openEl: null,

    build() {
      const bar = document.getElementById("menubar");
      if (!bar) return;
      bar.innerHTML = "";
      menus().forEach((m) => {
        const menu = document.createElement("div");
        menu.className = "menu";
        const btn = document.createElement("button");
        btn.className = "menu-btn";
        btn.textContent = m.label;
        menu.appendChild(btn);

        const dd = document.createElement("div");
        dd.className = "menu-dropdown";
        m.items.forEach((it) => {
          if (it.sep) { const s = document.createElement("div"); s.className = "menu-sep"; dd.appendChild(s); return; }
          const row = document.createElement("div");
          row.className = "menu-item";
          row.innerHTML = `<span>${it.label}</span>` + (it.acc ? `<span class="acc">${it.acc}</span>` : "");
          row.onclick = (e) => { e.stopPropagation(); this.closeAll(); try { it.act(); } catch (err) { console.error(err); } };
          dd.appendChild(row);
        });
        menu.appendChild(dd);

        btn.onclick = (e) => {
          e.stopPropagation();
          if (menu.classList.contains("open")) this.closeAll();
          else { this.closeAll(); menu.classList.add("open"); this._openEl = menu; }
        };
        // «меню-бар»: если одно открыто — наведение на соседа переключает
        btn.onmouseenter = () => {
          if (this._openEl && this._openEl !== menu) { this.closeAll(); menu.classList.add("open"); this._openEl = menu; }
        };
        bar.appendChild(menu);
      });
    },

    closeAll() {
      const b = document.getElementById("menubar");
      if (b) b.querySelectorAll(".menu.open").forEach((m) => m.classList.remove("open"));
      this._openEl = null;
    },

    bindWindowControls() {
      const min = document.getElementById("win-min");
      const max = document.getElementById("win-max");
      const close = document.getElementById("win-close");
      if (isEl && API.windowControls) {
        if (min) min.onclick = () => API.windowControls.minimize();
        if (max) max.onclick = () => API.windowControls.toggleMaximize();
        if (close) close.onclick = () => API.windowControls.close();
        if (API.windowControls.onMaximized && max) {
          API.windowControls.onMaximized((m) => { max.innerHTML = m ? "&#10064;" : "&#9634;"; max.title = m ? "Восстановить" : "Развернуть"; });
        }
      }
    },

    bindTitle() {
      const t = document.getElementById("appbar-title");
      if (t) t.ondblclick = () => AD.app.renameProject();
    },

    bindAccelerators() {
      window.addEventListener("keydown", (e) => {
        const typing = /INPUT|TEXTAREA|SELECT/.test((document.activeElement || {}).tagName || "");
        if (e.key === "F11") { e.preventDefault(); toggleFullscreen(); return; }
        if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
        const k = e.key.toLowerCase();
        const map = {
          n: () => AD.app.newDiagram(),
          o: () => AD.app.openProject(),
          s: () => AD.app.exportFile(),
          e: () => AD.exportMedia.open(),
        };
        if (k === "q" && isEl) { e.preventDefault(); API.windowControls.close(); return; }
        if (map[k] && !typing) { e.preventDefault(); map[k](); }
      });
    },

    init() {
      this.build();
      this.bindWindowControls();
      this.bindTitle();
      this.bindAccelerators();
      // закрытие меню по клику вне и по Esc
      window.addEventListener("mousedown", (e) => { if (!e.target.closest(".menu")) this.closeAll(); });
      window.addEventListener("keydown", (e) => { if (e.key === "Escape") this.closeAll(); });
    },
  });

  window.addEventListener("DOMContentLoaded", () => Menu.init());
})();
