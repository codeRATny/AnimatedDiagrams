/* =============================================================================
 * engine.js — детерминированный движок воспроизведения сценария
 *   Всё, что видно на экране в момент t, — чистая функция от t.
 *   Это даёт корректную перемотку (scrub), паузу и изменение скорости.
 * =========================================================================== */
(function () {
  "use strict";
  const AD = window.AD;
  const U = AD.util;
  const S = AD.store;
  const SVGNS = "http://www.w3.org/2000/svg";
  function el(name, attrs) {
    const e = document.createElementNS(SVGNS, name);
    if (attrs) for (const k in attrs) e.setAttribute(k, attrs[k]);
    return e;
  }

  const Engine = (AD.engine = {
    t: 0, // текущее время, мс
    playing: false,
    speed: 1,
    loop: true,
    _raf: null,
    _last: 0,

    init() {
      AD.diagram.stateResolver = (id) => this.stateAt(id, this.t);
      U.on("ad:model", () => {
        if (!this.playing) this.renderAt(this.t);
      });
    },

    duration() {
      return S.model.scenario.duration || 1;
    },

    /* --- состояние узла в момент t --------------------------------------
     * Возвращает выигравший state-шаг (или null). Состояние действует в пределах
     * своего блока [start, start+duration). Если активны несколько — побеждает
     * начавшийся позже (самый свежий). Вне блоков — состояние по умолчанию.
     */
    stateAt(nodeId, t) {
      let chosen = null, chosenStart = -1;
      for (const s of S.model.scenario.steps) {
        if (s.type !== "state" || s.nodeId !== nodeId) continue;
        if (s.start <= t && t < s.start + s.duration && s.start >= chosenStart) {
          chosen = s;
          chosenStart = s.start;
        }
      }
      return chosen;
    },

    /* --- активные шаги --- */
    activeSteps(t) {
      return S.model.scenario.steps.filter((s) => t >= s.start && t <= s.start + s.duration);
    },

    /* --- управление --- */
    play() {
      if (this.playing) return;
      if (this.t >= this.duration() - 1) this.t = 0;
      this.playing = true;
      this._last = performance.now();
      U.emit("ad:play", true);
      const tick = (now) => {
        if (!this.playing) return;
        const dt = (now - this._last) * this.speed;
        this._last = now;
        this.t += dt;
        if (this.t >= this.duration()) {
          if (this.loop) this.t = 0;
          else {
            this.t = this.duration();
            this.pause();
          }
        }
        this.renderAt(this.t);
        U.emit("ad:time", this.t);
        this._raf = requestAnimationFrame(tick);
      };
      this._raf = requestAnimationFrame(tick);
    },
    pause() {
      this.playing = false;
      if (this._raf) cancelAnimationFrame(this._raf);
      U.emit("ad:play", false);
    },
    toggle() {
      this.playing ? this.pause() : this.play();
    },
    stop() {
      this.pause();
      this.seek(0);
    },
    seek(t) {
      this.t = U.clamp(t, 0, this.duration());
      this.renderAt(this.t);
      U.emit("ad:time", this.t);
    },
    setSpeed(x) {
      this.speed = x;
    },

    /* --- отрисовка кадра --- */
    renderAt(t) {
      this.t = t; // синхронизируем время: состояние узлов резолвится по this.t (важно для экспорта)
      AD.diagram.highlights = {};
      const active = this.activeSteps(t);

      // подсветки/пульсы вычисляем ДО renderNodes
      for (const s of active) {
        const p = (t - s.start) / Math.max(1, s.duration);
        if (s.type === "pulse") {
          const pulse = 0.5 + 0.5 * Math.sin(p * Math.PI * 3);
          AD.diagram.highlights[s.nodeId] = {
            ring: s.color || "#22d3ee",
            opacity: 0.35 + 0.55 * pulse,
            scale: 1 + 0.05 * pulse,
          };
        }
        if (s.type === "message") {
          // мигающая подсветка источника в начале и цели в конце
          if (p < 0.2) this._hl(s.from, AD.MSG_VARIANTS[s.variant]);
          if (p > 0.8) this._hl(s.to, AD.MSG_VARIANTS[s.variant]);
        }
        if (s.type === "action") this._hl(s.nodeId, { color: s.color || "#38bdf8" });
      }

      AD.diagram.render();
      this.renderOverlay(t, active);
    },
    _hl(nodeId, variant) {
      if (!nodeId) return;
      const cur = AD.diagram.highlights[nodeId];
      if (cur && cur.scale) return;
      AD.diagram.highlights[nodeId] = { ring: (variant && variant.color) || "#60a5fa", opacity: 0.7 };
    },

    renderOverlay(t, active) {
      const g = AD.diagram.layers.overlay;
      g.innerHTML = "";
      for (const s of active) {
        const p = U.clamp((t - s.start) / Math.max(1, s.duration), 0, 1);
        if (s.type === "message") this.drawMessage(g, s, p);
        else if (s.type === "timer") this.drawTimer(g, s, p);
        else if (s.type === "note") this.drawNote(g, s, p);
        else if (s.type === "action") this.drawAction(g, s, p);
        else if (s.type === "link") this.drawLink(g, s, p);
      }
    },

    drawMessage(g, s, p) {
      const a = S.node(s.from), b = S.node(s.to);
      if (!a || !b) return;
      const variant = AD.MSG_VARIANTS[s.variant] || AD.MSG_VARIANTS.request;
      const ep = U.easeInOut(p);

      // сообщение идёт по существующей связи: сначала по явному edgeId, иначе — любой связи между узлами
      const edge = s.edgeId ? S.edge(s.edgeId) : S.edgeBetween(s.from, s.to);
      const pathEl = edge ? AD.diagram.edgePathEl(edge.id) : null;

      let pos, ang, dstr;
      if (pathEl) {
        const L = pathEl.getTotalLength() || 1;
        const forward = edge.from === s.from; // вдоль связи или против
        const len = forward ? ep * L : (1 - ep) * L;
        pos = pathEl.getPointAtLength(len);
        const nxt = pathEl.getPointAtLength(Math.max(0, Math.min(L, len + (forward ? 1 : -1))));
        ang = (Math.atan2(nxt.y - pos.y, nxt.x - pos.x) * 180) / Math.PI;
        dstr = pathEl.getAttribute("d");
      } else {
        // запасной путь — прямая между границами узлов (связи нет)
        const G = AD.geom;
        const ca = G.nodeCenter(a), cb = G.nodeCenter(b);
        const start = G.borderPoint(ca.x, ca.y, a.w / 2 + 3, a.h / 2 + 3, cb.x, cb.y);
        const end = G.borderPoint(cb.x, cb.y, b.w / 2 + 8, b.h / 2 + 8, ca.x, ca.y);
        pos = { x: U.lerp(start.x, end.x, ep), y: U.lerp(start.y, end.y, ep) };
        ang = (Math.atan2(end.y - start.y, end.x - start.x) * 180) / Math.PI;
        dstr = G.linePathD(start, end);
      }

      // «хвост» — путь связи (лёгкое свечение)
      g.appendChild(el("path", {
        d: dstr, fill: "none", stroke: variant.color, "stroke-width": 3,
        opacity: 0.25, "stroke-dasharray": variant.dash || "",
      }));

      const pk = el("g", { transform: `translate(${pos.x},${pos.y}) rotate(${ang})` });
      // капсула-пакет
      pk.appendChild(el("rect", {
        x: -13, y: -8, width: 26, height: 16, rx: 8,
        fill: variant.color, filter: "url(#glow)",
      }));
      pk.appendChild(el("path", {
        d: "M2,-4 L8,0 L2,4 z", fill: "#0b1220", opacity: 0.85,
      }));
      g.appendChild(pk);

      if (s.label) {
        const lg = el("g", { transform: `translate(${pos.x},${pos.y - 16})` });
        g.appendChild(lg); // добавляем в DOM, чтобы измерить фактическую ширину текста
        const tx = el("text", { x: 0, y: 0, class: "msg-label", "text-anchor": "middle", fill: variant.color });
        tx.textContent = s.label;
        lg.appendChild(tx);
        // плашка строится по реальной ширине текста (getComputedTextLength), а не по числу символов
        const tw = tx.getComputedTextLength() + 14;
        const bg = el("rect", { x: -tw / 2, y: -13, width: tw, height: 18, rx: 5, fill: "#0b1220", opacity: 0.82, stroke: variant.color, "stroke-width": 1 });
        lg.insertBefore(bg, tx); // фон под текстом
      }
    },

    drawTimer(g, s, p) {
      const n = S.node(s.nodeId);
      if (!n) return;
      const total = s.seconds || Math.round(s.duration / 1000);
      const remaining = Math.max(0, total * (1 - p));
      const cx = n.x + n.w - 6, cy = n.y - 6, r = 20;
      const circ = 2 * Math.PI * r;
      const grp = el("g", { transform: `translate(${cx},${cy})` });
      grp.appendChild(el("circle", { r: r + 4, fill: "#0b1220", opacity: 0.9 }));
      grp.appendChild(el("circle", { r, fill: "none", stroke: "#334155", "stroke-width": 4 }));
      const danger = remaining <= 1.5;
      grp.appendChild(el("circle", {
        r, fill: "none",
        stroke: danger ? "#ef4444" : "#f59e0b",
        "stroke-width": 4, "stroke-linecap": "round",
        "stroke-dasharray": circ,
        "stroke-dashoffset": circ * p,
        transform: "rotate(-90)",
      }));
      const unit = AD.TIME_UNITS[s.unit] || AD.TIME_UNITS.s;
      const label = Math.ceil(remaining) + unit.short;
      const tx = el("text", { x: 0, y: 5, "text-anchor": "middle", class: "timer-text", fill: danger ? "#fca5a5" : "#fcd34d" });
      tx.textContent = label;
      // ужимаем шрифт, чтобы длинная подпись помещалась в бейдж
      if (label.length >= 5) tx.style.fontSize = "10px";
      else if (label.length === 4) tx.style.fontSize = "12px";
      grp.appendChild(tx);
      if (s.label) {
        const lb = el("text", { x: 0, y: r + 15, "text-anchor": "middle", class: "timer-sub" });
        lb.textContent = s.label;
        grp.appendChild(lb);
      }
      g.appendChild(grp);
    },

    drawNote(g, s, p) {
      const fade = p < 0.12 ? p / 0.12 : p > 0.88 ? (1 - p) / 0.12 : 1;
      const text = s.text || "заметка";
      const grp = el("g", { transform: `translate(${s.x || 40},${s.y || 40})`, opacity: fade });
      g.appendChild(grp); // в DOM — чтобы измерить текст
      const tx = el("text", { x: 14, y: 19, class: "note-text" });
      tx.textContent = text;
      grp.appendChild(tx);
      // плашка по фактической ширине текста (левый отступ 14 + правый 14)
      const w = tx.getComputedTextLength() + 28;
      const color = s.color || "#fbbf24";
      grp.insertBefore(el("rect", { x: 0, y: 0, width: w, height: 30, rx: 8, fill: "#111a2e", stroke: color, "stroke-width": 1.5, filter: "url(#nodeShadow)" }), tx);
      // clip по форме плашки, чтобы левая акцентная полоса не вылезала за скруглённые углы
      const clipId = "noteclip-" + s.id;
      const clip = el("clipPath", { id: clipId });
      clip.appendChild(el("rect", { x: 0, y: 0, width: w, height: 30, rx: 8, ry: 8 }));
      grp.insertBefore(clip, tx);
      grp.insertBefore(el("rect", { x: 0, y: 0, width: 5, height: 30, fill: color, "clip-path": "url(#" + clipId + ")" }), tx);
    },

    /* Действие на сервисе: бейдж под узлом с текстом действия и вращающимся спиннером */
    drawAction(g, s, p) {
      const n = S.node(s.nodeId);
      if (!n) return;
      const color = s.color || "#38bdf8";
      const text = s.text || "Действие";
      const grp = el("g", { transform: `translate(${n.x + n.w / 2},${n.y + n.h + 18})` });
      g.appendChild(grp); // в DOM — чтобы измерить текст
      // текст сначала (для измерения ширины)
      const tx = el("text", { x: 0, y: 4, class: "action-label", fill: color, "text-anchor": "start" });
      tx.textContent = text;
      grp.appendChild(tx);
      const tw = tx.getComputedTextLength();
      const pad = 12, sr = 7, gap = 7;        // отступ, радиус спиннера, зазор
      const W = pad + sr * 2 + gap + tw + pad; // ширина пилюли по содержимому
      const leftX = -W / 2;
      // фон-пилюля
      grp.insertBefore(el("rect", { x: leftX, y: -13, width: W, height: 26, rx: 13, fill: "#0b1220", opacity: 0.92, stroke: color, "stroke-width": 1.2, filter: "url(#nodeShadow)" }), tx);
      // вращающийся спиннер (индикатор активности) — вращение по абсолютному времени
      const scx = leftX + pad + sr;
      const circ = 2 * Math.PI * sr;
      const spin = el("g", { transform: `translate(${scx},0) rotate(${(this.t / 1000 * 300) % 360})` });
      spin.appendChild(el("circle", { r: sr, fill: "none", stroke: color, opacity: 0.25, "stroke-width": 2.4 }));
      spin.appendChild(el("circle", {
        r: sr, fill: "none", stroke: color, "stroke-width": 2.4, "stroke-linecap": "round",
        "stroke-dasharray": circ * 0.65 + " " + circ,
      }));
      grp.insertBefore(spin, tx);
      // сдвигаем текст правее спиннера
      tx.setAttribute("x", scx + sr + gap);
    },

    /* Соединение: поверх связи рисуется линия заданного цвета с выбранной анимацией
       (бегущий пунктир / пунктир / сплошная / пульсация), а у связи — бейдж с текстом.
       Текст, цвет, анимацию, положение и размер шрифта задаёт пользователь. */
    drawLink(g, s, p) {
      const edge = s.edgeId ? S.edge(s.edgeId) : null;
      const pathEl = edge ? AD.diagram.edgePathEl(edge.id) : null;
      if (!pathEl) return;
      const color = s.color || "#f87171";
      const anim = AD.LINK_ANIMS[s.anim] || AD.LINK_ANIMS.flow;
      const d = pathEl.getAttribute("d");

      // оверлей поверх связи — цвет + анимация
      const line = el("path", { d, fill: "none", stroke: color, "stroke-linecap": "round" });
      let sw = 4, op = 0.9;
      if (anim.dash) line.setAttribute("stroke-dasharray", anim.dash);
      if (anim.flow) line.setAttribute("stroke-dashoffset", ((-this.t / 1000 * 26) % 1000).toFixed(1)); // бегущий пунктир
      if (anim.pulse) { const k = 0.5 + 0.5 * Math.sin((this.t / 1000) * Math.PI * 2); sw = 3 + 2.5 * k; op = 0.45 + 0.45 * k; }
      line.setAttribute("stroke-width", sw);
      line.setAttribute("opacity", op);
      g.appendChild(line);

      // бейдж с текстом: положение вдоль связи (labelPos) + перпендикулярное смещение (labelOff),
      // размер шрифта (labelSize) — всё настраивается в свойствах шага
      if (!s.text) return;
      const fs = s.labelSize || 12;
      const pos = s.labelPos != null ? s.labelPos : 0.5;
      const off = s.labelOff != null ? s.labelOff : 22;
      const pt = AD.geom.pointAlongPath(pathEl, pos, off);
      const grp = el("g", { transform: `translate(${pt.x},${pt.y})` });
      g.appendChild(grp); // в DOM — чтобы измерить текст
      const tx = el("text", { x: 0, y: 0, class: "action-label", fill: color, "text-anchor": "middle", "dominant-baseline": "middle" });
      tx.style.fontSize = fs + "px";
      tx.textContent = s.text;
      grp.appendChild(tx);
      const tw = tx.getComputedTextLength();
      const h = Math.max(24, fs + 12), padX = Math.max(12, fs);
      const W = tw + padX * 2;
      grp.insertBefore(el("rect", { x: -W / 2, y: -h / 2, width: W, height: h, rx: h / 2, fill: "#0b1220", opacity: 0.95, stroke: color, "stroke-width": 1.2, filter: "url(#nodeShadow)" }), tx);
    },
  });
})();
