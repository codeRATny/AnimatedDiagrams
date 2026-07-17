/* =============================================================================
 * export-media.js — экспорт анимации в GIF (gifenc) и WebM (MediaRecorder)
 *   Опирается на детерминированный AD.engine.renderAt(t): гоняем кадры по
 *   сетке времени, растрируем каждый SVG-кадр в canvas и кодируем.
 * =========================================================================== */
(function () {
  "use strict";
  const AD = window.AD;
  const S = AD.store;
  const SVGNS = "http://www.w3.org/2000/svg";

  /* CSS, задающий оформление, которое берётся из классов, а не из атрибутов.
     Инлайним его в клонированный SVG, иначе текст/линии потеряют стиль при растре. */
  const EXPORT_CSS = `
    text { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; }
    .edge { stroke: #5f7196; stroke-width: 2.2; }
    .edge-hit { stroke: none; fill: none; }
    .edge-label { fill: #9fb3d6; font-size: 12px; paint-order: stroke; stroke: #0a111f; stroke-width: 3px; }
    .node-label { fill: #f2f6ff; font-size: 15px; font-weight: 600; }
    .node-sub { fill: #b7c6e4; font-size: 11px; opacity: .85; }
    .node-icon { font-size: 16px; }
    .msg-label { font-size: 11px; font-weight: 600; }
    .timer-text { font-size: 15px; font-weight: 700; }
    .timer-sub { fill: #cbd5e1; font-size: 10px; }
    .note-text { fill: #e6eefc; font-size: 12.5px; }
  `;

  const Exp = (AD.exportMedia = {
    modal: null,
    _cancel: false,
    _running: false,

    /* ---- модалка ---------------------------------------------------------- */
    open(format) {
      this.buildModal();
      if (format) this._setRadio(format);
      this.modal.classList.add("show");
      this._progress("", 0);
      this.updateEstimate();
    },
    close() {
      if (this._running) return;
      if (this.modal) this.modal.classList.remove("show");
    },

    buildModal() {
      if (this.modal) return;
      const m = document.createElement("div");
      m.className = "modal-overlay";
      m.id = "export-modal";
      m.innerHTML = `
        <div class="modal">
          <div class="modal-head">
            <h2>Экспорт анимации</h2>
            <button class="modal-x" id="exp-x">✕</button>
          </div>
          <div class="modal-body">
            <div class="exp-formats">
              <label class="exp-fmt"><input type="radio" name="exp-fmt" value="gif" checked> <b>GIF</b><span>Универсально, зацикленно</span></label>
              <label class="exp-fmt"><input type="radio" name="exp-fmt" value="webm"> <b>WebM</b><span>Лучше качество/размер</span></label>
            </div>
            <div class="row2">
              <label class="field"><span>Частота кадров</span>
                <select id="exp-fps">
                  <option value="8">8 fps</option>
                  <option value="12">12 fps</option>
                  <option value="15" selected>15 fps</option>
                  <option value="20">20 fps</option>
                  <option value="24">24 fps</option>
                  <option value="30">30 fps</option>
                  <option value="45">45 fps</option>
                  <option value="60">60 fps</option>
                </select>
              </label>
              <label class="field"><span>Масштаб (разрешение)</span>
                <select id="exp-scale">
                  <option value="1" selected>1×</option>
                  <option value="1.5">1.5×</option>
                  <option value="2">2×</option>
                  <option value="3">3×</option>
                  <option value="4">4×</option>
                  <option value="6">6×</option>
                  <option value="8">8×</option>
                  <option value="10">10×</option>
                </select>
              </label>
            </div>
            <div class="row2">
              <label class="field"><span>Кадрирование</span>
                <select id="exp-mode">
                  <option value="content" selected>По содержимому</option>
                  <option value="view">Как на экране</option>
                </select>
              </label>
              <label class="field"><span>Фон</span>
                <span class="exp-bg">
                  <input type="color" id="exp-bg" value="#0a111f">
                  <button class="chip" data-bg="#0a111f" title="Тёмный">🌑</button>
                  <button class="chip" data-bg="#ffffff" title="Белый">⬜</button>
                </span>
              </label>
            </div>
            <label class="exp-loop" id="exp-loop-wrap"><input type="checkbox" id="exp-loop" checked> Зациклить (GIF)</label>
            <p class="exp-estimate" id="exp-estimate"></p>
            <div class="exp-progress"><div class="exp-progress-bar" id="exp-bar"></div></div>
            <p class="exp-status muted" id="exp-status"></p>
          </div>
          <div class="modal-foot">
            <button class="btn" id="exp-cancel">Отмена</button>
            <button class="btn primary" id="exp-go">Экспортировать</button>
          </div>
        </div>`;
      document.body.appendChild(m);
      this.modal = m;

      const $ = (s) => m.querySelector(s);
      $("#exp-x").onclick = () => this.close();
      $("#exp-cancel").onclick = () => {
        if (this._running) this._cancel = true;
        else this.close();
      };
      $("#exp-go").onclick = () => this.start();
      $("#exp-fps").onchange = () => this.updateEstimate();
      $("#exp-scale").onchange = () => this.updateEstimate();
      $("#exp-mode").onchange = () => this.updateEstimate();
      m.querySelectorAll('input[name="exp-fmt"]').forEach((r) => {
        r.onchange = () => {
          $("#exp-loop-wrap").style.display = this._fmt() === "gif" ? "" : "none";
          this.updateEstimate();
        };
      });
      m.querySelectorAll(".chip").forEach((c) => {
        c.onclick = () => { $("#exp-bg").value = c.dataset.bg; };
      });
      m.addEventListener("mousedown", (e) => { if (e.target === m) this.close(); });
    },

    _fmt() {
      return this.modal.querySelector('input[name="exp-fmt"]:checked').value;
    },
    _setRadio(fmt) {
      const r = this.modal.querySelector(`input[name="exp-fmt"][value="${fmt}"]`);
      if (r) { r.checked = true; r.dispatchEvent(new Event("change")); }
    },
    readOpts() {
      const $ = (s) => this.modal.querySelector(s);
      return {
        fmt: this._fmt(),
        fps: parseInt($("#exp-fps").value, 10),
        scale: parseFloat($("#exp-scale").value),
        mode: $("#exp-mode").value,
        bg: $("#exp-bg").value,
        loop: $("#exp-loop").checked,
      };
    },
    updateEstimate() {
      if (!this.modal) return;
      const o = this.readOpts();
      const geom = this.computeBounds(o.mode, o.scale);
      const n = Math.max(1, Math.round(AD.engine.duration() / 1000 * o.fps)) + 1;
      const mp = (geom.pxW * geom.pxH) / 1e6; // мегапиксели кадра
      const heavy = n > 200 || mp > 6;
      const gifNote = o.fmt === "gif" && (mp > 4 || n > 200) ? " Для больших/долгих — берите WebM." : "";
      const warn = heavy ? `  ⚠ большой объём: ${n} кадров × ${mp.toFixed(1)}МП — экспорт долгий, файл большой.${gifNote}` : "";
      this.modal.querySelector("#exp-estimate").textContent =
        `≈ ${n} кадров · ${geom.pxW}×${geom.pxH}px · ${(AD.engine.duration() / 1000).toFixed(1)}с${warn}`;
    },

    async start() {
      if (this._running) return;
      const o = this.readOpts();
      this._running = true;
      this._cancel = false;
      this.modal.querySelector("#exp-go").disabled = true;
      this.modal.querySelector("#exp-cancel").textContent = "Прервать";
      try {
        if (o.fmt === "gif") await this.runGif(o);
        else await this.runWebm(o);
      } finally {
        this._running = false;
        this.modal.querySelector("#exp-go").disabled = false;
        this.modal.querySelector("#exp-cancel").textContent = "Закрыть";
      }
    },

    /* ---- геометрия кадра -------------------------------------------------- */
    computeBounds(mode, scale) {
      const svg = AD.diagram.svg;
      let b;
      if (mode === "view") {
        const r = svg.getBoundingClientRect();
        const v = S.model.view;
        b = { x: -v.panX / v.zoom, y: -v.panY / v.zoom, w: r.width / v.zoom, h: r.height / v.zoom };
      } else {
        const ns = S.model.nodes;
        if (!ns.length) {
          b = { x: 0, y: 0, w: 640, h: 360 };
        } else {
          let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
          for (const n of ns) {
            minX = Math.min(minX, n.x); minY = Math.min(minY, n.y);
            maxX = Math.max(maxX, n.x + n.w); maxY = Math.max(maxY, n.y + n.h);
          }
          for (const s of S.model.scenario.steps) {
            if (s.type === "note") {
              const nx = s.x || 0, ny = s.y || 0;
              minX = Math.min(minX, nx); minY = Math.min(minY, ny - 6);
              maxX = Math.max(maxX, nx + 260); maxY = Math.max(maxY, ny + 32);
            }
          }
          const pad = 74; // место под кольцо таймера и подписи пакетов
          b = { x: minX - pad, y: minY - pad, w: maxX - minX + pad * 2, h: maxY - minY + pad * 2 };
        }
      }
      b.w = Math.max(40, b.w); b.h = Math.max(40, b.h);
      let pxW = Math.round(b.w * scale), pxH = Math.round(b.h * scale);
      // защитный предел по большей стороне (лимит canvas в браузере ~16384px), с сохранением пропорций
      const MAXD = 12000;
      const k = Math.min(1, MAXD / pxW, MAXD / pxH);
      if (k < 1) { pxW = Math.round(pxW * k); pxH = Math.round(pxH * k); }
      pxW += pxW % 2; pxH += pxH % 2; // чётные размеры для видеокодеков
      return { world: b, pxW, pxH };
    },

    /* ---- растр одного кадра ---------------------------------------------- */
    async renderFrameToCanvas(t, ctx, geom, bg) {
      AD.engine.renderAt(t);
      const clone = AD.diagram.svg.cloneNode(true);
      const vp = clone.querySelector("#viewport");
      if (vp) vp.removeAttribute("transform");
      clone.setAttribute("xmlns", SVGNS);
      clone.setAttribute("viewBox", `${geom.world.x} ${geom.world.y} ${geom.world.w} ${geom.world.h}`);
      clone.setAttribute("width", geom.pxW);
      clone.setAttribute("height", geom.pxH);
      clone.setAttribute("preserveAspectRatio", "xMidYMid meet");
      const style = document.createElementNS(SVGNS, "style");
      style.textContent = EXPORT_CSS;
      clone.insertBefore(style, clone.firstChild);
      const str = new XMLSerializer().serializeToString(clone);
      const url = URL.createObjectURL(new Blob([str], { type: "image/svg+xml;charset=utf-8" }));
      try {
        const img = await this._loadImage(url);
        ctx.clearRect(0, 0, geom.pxW, geom.pxH);
        ctx.fillStyle = bg;
        ctx.fillRect(0, 0, geom.pxW, geom.pxH);
        ctx.drawImage(img, 0, 0, geom.pxW, geom.pxH);
      } finally {
        URL.revokeObjectURL(url);
      }
    },
    _loadImage(url) {
      return new Promise((res, rej) => {
        const img = new Image();
        img.onload = () => res(img);
        img.onerror = () => rej(new Error("не удалось растрировать SVG-кадр"));
        img.src = url;
      });
    },

    /* ---- сохранение/восстановление состояния сцены ----------------------- */
    _saveState() {
      const st = { t: AD.engine.t, sel: S.selection, conn: AD.diagram._connectFrom };
      AD.engine.pause();
      S.selection = { type: null, id: null };
      AD.diagram._connectFrom = null;
      return st;
    },
    _restoreState(st) {
      S.selection = st.sel;
      AD.diagram._connectFrom = st.conn;
      AD.engine.seek(st.t);
    },

    /* ---- GIF -------------------------------------------------------------- */
    async runGif(o) {
      if (!window.gifenc) { AD.app.toast("GIF-энкодер не загружен", true); return null; }
      const { GIFEncoder, quantize, applyPalette } = window.gifenc;
      const geom = this.computeBounds(o.mode, o.scale);
      const canvas = document.createElement("canvas");
      canvas.width = geom.pxW; canvas.height = geom.pxH;
      const ctx = canvas.getContext("2d", { willReadFrequently: true });
      const duration = AD.engine.duration();
      const nFrames = Math.max(1, Math.round(duration / 1000 * o.fps));
      const delay = Math.round(1000 / o.fps);
      const gif = GIFEncoder();
      const st = this._saveState();
      try {
        for (let i = 0; i <= nFrames; i++) {
          if (this._cancel) { this._progress("Прервано", 0); return null; }
          const t = Math.min(duration, (i / o.fps) * 1000);
          await this.renderFrameToCanvas(t, ctx, geom, o.bg);
          const { data } = ctx.getImageData(0, 0, geom.pxW, geom.pxH);
          const palette = quantize(data, 256);
          const index = applyPalette(data, palette);
          const fopts = { palette, delay };
          if (i === 0) fopts.repeat = o.loop ? 0 : -1;
          gif.writeFrame(index, geom.pxW, geom.pxH, fopts);
          this._progress(`Кадр ${i + 1}/${nFrames + 1}`, (i + 1) / (nFrames + 1));
          if (i % 3 === 0) await this._yield();
        }
        gif.finish();
        const bytes = gif.bytes();
        const blob = new Blob([bytes], { type: "image/gif" });
        this._download(blob, "gif");
        this._progress(`Готово · ${this._kb(bytes.length)}`, 1);
        return blob;
      } catch (e) {
        AD.app.toast("Ошибка экспорта GIF: " + e.message, true);
        this._progress("Ошибка: " + e.message, 0);
        return null;
      } finally {
        this._restoreState(st);
      }
    },

    /* ---- WebM ------------------------------------------------------------- */
    async runWebm(o) {
      if (typeof MediaRecorder === "undefined") { AD.app.toast("WebM не поддерживается браузером", true); return null; }
      const geom = this.computeBounds(o.mode, o.scale);
      const canvas = document.createElement("canvas");
      canvas.width = geom.pxW; canvas.height = geom.pxH;
      const ctx = canvas.getContext("2d");
      const duration = AD.engine.duration();
      const mime = ["video/webm;codecs=vp9", "video/webm;codecs=vp8", "video/webm"]
        .find((mm) => MediaRecorder.isTypeSupported && MediaRecorder.isTypeSupported(mm)) || "video/webm";
      const stream = canvas.captureStream(o.fps);
      let rec;
      try {
        rec = new MediaRecorder(stream, { mimeType: mime, videoBitsPerSecond: 8000000 });
      } catch (e) {
        AD.app.toast("MediaRecorder: " + e.message, true);
        return null;
      }
      const chunks = [];
      rec.ondataavailable = (e) => { if (e.data && e.data.size) chunks.push(e.data); };
      const stopped = new Promise((res) => { rec.onstop = res; });
      const st = this._saveState();
      try {
        await this.renderFrameToCanvas(0, ctx, geom, o.bg); // первый кадр до старта
        rec.start();
        // реал-тайм прогон: MediaRecorder сам сэмплирует canvas на частоте fps
        const t0 = performance.now();
        for (;;) {
          if (this._cancel) break;
          const t = Math.min(duration, performance.now() - t0);
          await this.renderFrameToCanvas(t, ctx, geom, o.bg);
          this._progress(`${(t / 1000).toFixed(1)}с / ${(duration / 1000).toFixed(1)}с`, t / duration);
          if (t >= duration) break;
          await this._raf();
        }
      } finally {
        if (rec.state !== "inactive") rec.stop();
        await stopped;
        this._restoreState(st);
      }
      if (this._cancel) { this._progress("Прервано", 0); return null; }
      const blob = new Blob(chunks, { type: "video/webm" });
      this._download(blob, "webm");
      this._progress(`Готово · ${this._kb(blob.size)}`, 1);
      return blob;
    },

    /* ---- утилиты ---------------------------------------------------------- */
    _download(blob, ext) {
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = (S.model.meta.name || "diagram").replace(/\s+/g, "_") + "." + ext;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 4000);
    },
    _progress(text, frac) {
      if (!this.modal) return;
      this.modal.querySelector("#exp-status").textContent = text;
      this.modal.querySelector("#exp-bar").style.width = Math.round((frac || 0) * 100) + "%";
    },
    _kb(bytes) {
      return bytes < 1024 * 1024 ? (bytes / 1024).toFixed(0) + " КБ" : (bytes / 1024 / 1024).toFixed(2) + " МБ";
    },
    _yield() { return new Promise((r) => setTimeout(r, 0)); },
    _raf() { return new Promise((r) => requestAnimationFrame(() => r())); },
  });
})();
