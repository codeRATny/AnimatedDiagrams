/* =============================================================================
 * timeline.js — редактор сценария (таймлайн в стиле видеоредактора)
 * =========================================================================== */
(function () {
  "use strict";
  const AD = window.AD;
  const U = AD.util;
  const S = AD.store;

  const Timeline = (AD.timeline = {
    root: null,
    ruler: null,
    tracks: null,
    playhead: null,
    _drag: null,
    pxPerSec: 90,
    minPxPerSec: 12,
    maxPxPerSec: 600,

    init(rootEl) {
      this.root = rootEl;
      this.ruler = rootEl.querySelector("#tl-ruler");
      this.tracks = rootEl.querySelector("#tl-tracks");
      this.playhead = rootEl.querySelector("#tl-playhead");
      this.rebuildScale();
      this.bind();
      U.on("ad:model", () => this.render());
      U.on("ad:time", () => this.updatePlayhead());
      U.on("ad:selection", () => this.render());
      this.render();
    },

    duration() {
      return S.model.scenario.duration || 1;
    },
    /** конец видимой шкалы = максимум из длительности сцены и конца всех событий,
        чтобы события за пределом сцены не «вылетали» за таймлайн */
    contentEnd() {
      let end = this.duration();
      for (const s of S.model.scenario.steps) end = Math.max(end, s.start + s.duration);
      return end;
    },

    /* --- преобразование время↔пиксели через готовую линейную шкалу d3-scale --- */
    rebuildScale() {
      // мс → пиксели при текущем масштабе (pxPerSec): 1000 мс = pxPerSec px
      this._scale = window.d3.scaleLinear().domain([0, 1000]).range([0, this.pxPerSec]);
    },
    timeToX(t) {
      return this._scale(t);
    },
    xToTime(px) {
      return this._scale.invert(px);
    },
    width() {
      return this.timeToX(this.contentEnd());
    },

    /** масштабирование таймлайна с привязкой к точке под курсором (Ctrl+scroll) */
    zoomAt(clientX, factor) {
      const scroller = this.root.querySelector("#tl-scroll");
      if (!scroller) return;
      const rect = scroller.getBoundingClientRect();
      const cursorX = clientX - rect.left;                 // позиция курсора в окне таймлайна
      const time = this.xToTime(scroller.scrollLeft + cursorX); // время под курсором (текущий масштаб)
      const next = U.clamp(this.pxPerSec * factor, this.minPxPerSec, this.maxPxPerSec);
      if (next === this.pxPerSec) return;
      this.pxPerSec = next;
      this.render();
      // возвращаем то же время под курсор
      scroller.scrollLeft = this.timeToX(time) - cursorX;
    },

    variantColor(s) {
      if (s.type === "message") return (AD.MSG_VARIANTS[s.variant] || {}).color || "#60a5fa";
      if (s.type === "timer") return "#f59e0b";
      if (s.type === "state") return s.color || (AD.NODE_STATES[s.state] || {}).ring || "#94a3b8";
      if (s.type === "action") return s.color || "#38bdf8";
      if (s.type === "link") return s.color || "#93c5fd";
      if (s.type === "note") return "#fbbf24";
      if (s.type === "pulse") return "#22d3ee";
      return "#64748b";
    },
    stepTitle(s) {
      const nm = (id) => (S.node(id) ? S.node(id).label : "?");
      if (s.type === "message") return `${nm(s.from)} → ${nm(s.to)}${s.label ? " · " + s.label : ""}`;
      if (s.type === "timer") return `⏱ ${nm(s.nodeId)} · ${s.seconds || Math.round(s.duration / 1000)}${(AD.TIME_UNITS[s.unit] || AD.TIME_UNITS.s).short}`;
      if (s.type === "state") return `⇄ ${nm(s.nodeId)} → ${s.label || (AD.NODE_STATES[s.state] || {}).label || s.state}`;
      if (s.type === "action") return `⚙ ${nm(s.nodeId)} · ${s.text || ""}`;
      if (s.type === "link") {
        const e = S.edge(s.edgeId);
        return `⚡ ${e ? nm(e.from) + " ↔ " + nm(e.to) : "?"}${s.text ? " · " + s.text : ""}`;
      }
      if (s.type === "note") return `✎ ${s.text || ""}`;
      if (s.type === "pulse") return `✷ ${nm(s.nodeId)}`;
      return s.type;
    },

    render() {
      if (!this.tracks) return;
      this.rebuildScale();
      const w = this.width();
      // линейка
      this.ruler.style.width = w + "px";
      this.ruler.innerHTML = "";
      const durX = this.timeToX(this.duration());
      // деления и подписи считает d3-scale (нужное число «круглых» значений и формат)
      const totalSec = this.contentEnd() / 1000;
      const secScale = window.d3.scaleLinear().domain([0, totalSec]).range([0, w]);
      const count = Math.max(2, Math.round(w / 70));
      const fmt = secScale.tickFormat(count);
      secScale.ticks(count).forEach((sec) => {
        const tick = document.createElement("div");
        tick.className = "tl-tick";
        tick.style.left = secScale(sec) + "px";
        tick.innerHTML = `<span>${fmt(sec)}с</span>`;
        this.ruler.appendChild(tick);
      });
      // отметка конца сцены (докуда идёт воспроизведение)
      const endMark = document.createElement("div");
      endMark.className = "tl-endmark";
      endMark.style.left = durX + "px";
      endMark.title = "Конец сцены — события правее не проигрываются";
      this.ruler.appendChild(endMark);

      // дорожки: непересекающиеся во времени события пакуются на одну дорожку,
      // а параллельные (перекрывающиеся) естественно расходятся по разным.
      const ROW_H = 28;
      this.tracks.style.width = w + "px";
      this.tracks.innerHTML = "";
      const steps = S.model.scenario.steps.slice().sort((a, b) => a.start - b.start);
      const laneEnds = []; // время конца последнего бара в каждой дорожке
      const laneOf = (s) => {
        for (let l = 0; l < laneEnds.length; l++) {
          if (laneEnds[l] <= s.start) { laneEnds[l] = s.start + s.duration; return l; }
        }
        laneEnds.push(s.start + s.duration);
        return laneEnds.length - 1;
      };
      steps.forEach((s) => {
        const lane = laneOf(s);
        const selected = S.selection.type === "step" && S.selection.id === s.id;
        const title = this.stepTitle(s);
        const bar = document.createElement("div");
        bar.className = "tl-bar tl-" + s.type + (selected ? " selected" : "");
        bar.style.left = this.timeToX(s.start) + "px";
        bar.style.width = Math.max(14, this.timeToX(s.duration)) + "px";
        bar.style.top = lane * ROW_H + 3 + "px";
        bar.style.setProperty("--c", this.variantColor(s));
        bar.dataset.step = s.id;
        bar.title = `${title}  ·  ${(s.start / 1000).toFixed(2)}–${((s.start + s.duration) / 1000).toFixed(2)}с`;
        bar.innerHTML =
          `<span class="tl-bar-label">${this.escape(title)}</span>` +
          `<span class="tl-handle tl-handle-l" data-h="l"></span>` +
          `<span class="tl-handle tl-handle-r" data-h="r"></span>`;
        this.tracks.appendChild(bar);
      });
      const tracksH = Math.max(90, laneEnds.length * ROW_H + 12);
      this.tracks.style.height = tracksH + "px";
      this.playhead.style.left = this.timeToX(AD.engine.t) + "px";
      this.playhead.style.height = 26 + tracksH + "px"; // линейка (26px) + дорожки
    },
    updatePlayhead() {
      this.playhead.style.left = this.timeToX(AD.engine.t) + "px";
    },
    escape(t) {
      return String(t).replace(/[<>&]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;" }[c]));
    },

    bind() {
      // Ctrl/Cmd + колесо -> масштабирование таймлайна к позиции курсора
      const scroller = this.root.querySelector("#tl-scroll");
      if (scroller) {
        scroller.title = "Ctrl + колесо — масштаб таймлайна";
        scroller.addEventListener("wheel", (ev) => {
          if (!ev.ctrlKey && !ev.metaKey) return; // обычный скролл не трогаем
          ev.preventDefault();
          const factor = ev.deltaY < 0 ? 1.15 : 1 / 1.15;
          this.zoomAt(ev.clientX, factor);
        }, { passive: false });
      }

      // клик по линейке -> перемотка
      this.ruler.addEventListener("mousedown", (ev) => {
        const r = this.ruler.getBoundingClientRect();
        AD.engine.pause();
        AD.engine.seek(this.xToTime(ev.clientX - r.left));
        this._drag = { mode: "scrub" };
      });

      // взаимодействие с барами
      this.tracks.addEventListener("mousedown", (ev) => {
        const bar = ev.target.closest(".tl-bar");
        if (!bar) return;
        const s = S.step(bar.dataset.step);
        if (!s) return;
        const handle = ev.target.dataset.h;
        S.select("step", s.id); // перерисует таймлайн (ad:selection) — берём свежий элемент ниже
        const el = this.tracks.querySelector('.tl-bar[data-step="' + CSS.escape(s.id) + '"]');
        this._drag = {
          mode: handle ? "resize-" + handle : "move",
          id: s.id,
          startX: ev.clientX,
          origStart: s.start,
          origDur: s.duration,
          el,
        };
        ev.stopPropagation();
      });

      window.addEventListener("mousemove", (ev) => {
        if (!this._drag) return;
        if (this._drag.mode === "scrub") {
          const r = this.ruler.getBoundingClientRect();
          AD.engine.seek(this.xToTime(ev.clientX - r.left));
          return;
        }
        const s = S.step(this._drag.id);
        if (!s) return;
        const dt = this.xToTime(ev.clientX - this._drag.startX);
        const snap = (v) => Math.max(0, Math.round(v / 50) * 50);
        if (this._drag.mode === "move") {
          s.start = snap(this._drag.origStart + dt);
        } else if (this._drag.mode === "resize-r") {
          s.duration = Math.max(100, snap(this._drag.origDur + dt));
        } else if (this._drag.mode === "resize-l") {
          const ns = snap(this._drag.origStart + dt);
          const delta = ns - this._drag.origStart;
          s.start = ns;
          s.duration = Math.max(100, this._drag.origDur - delta);
        }
        // во время перетаскивания двигаем только сам блок (без перепаковки дорожек),
        // чтобы он не «прыгал» вертикально; полная перепаковка — на отпускании
        if (this._drag.el) {
          this._drag.el.style.left = this.timeToX(s.start) + "px";
          this._drag.el.style.width = Math.max(14, this.timeToX(s.duration)) + "px";
        } else {
          this.render();
        }
        AD.engine.renderAt(AD.engine.t);
        U.emit("ad:step-live", s.id);
      });

      window.addEventListener("mouseup", () => {
        if (this._drag && this._drag.id) {
          S.sortSteps();
          S.autoDuration();
          S.touch();
        }
        this._drag = null;
      });
    },
  });
})();
