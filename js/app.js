/* =============================================================================
 * app.js — сборка UI: тулбар, инспектор, управление воспроизведением, шаги
 * =========================================================================== */
(function () {
  "use strict";
  const AD = window.AD;
  const U = AD.util;
  const S = AD.store;
  const $ = (sel, root) => (root || document).querySelector(sel);

  const App = (AD.app = {
    init() {
      AD.diagram.init($("#stage"));
      AD.engine.init();
      AD.timeline.init($("#timeline"));

      if (!S.load()) S.replace(AD.sampleModel());
      AD.diagram.render();
      AD.diagram.fitView();

      this.bindTopbar();
      this.bindTools();
      this.bindPlayback();
      this.bindStepAdder();
      this.bindDockResize();
      this.bindKeys();

      U.on("ad:selection", () => this.renderInspector(true));
      U.on("ad:model", () => this.renderInspector(false));
      U.on("ad:step-live", () => this.renderInspector(false));
      U.on("ad:play", (e) => this.reflectPlay(e.detail));
      U.on("ad:time", () => this.reflectTime());
      U.on("ad:tool", (e) => this.reflectTool(e.detail));

      this.renderInspector();
      this.reflectTool("select");
      AD.engine.renderAt(0);
      this.setProjName();

      // deep-link: ?t=<мс> — кадр, ?play=1 — запуск, ?export=gif|webm — открыть экспорт
      const q = new URLSearchParams(location.search);
      if (q.has("t")) AD.engine.seek(parseFloat(q.get("t")) || 0);
      if (q.get("play") === "1") AD.engine.play();
      if (q.has("export")) AD.exportMedia.open(q.get("export") || undefined);
    },

    setProjName() {
      const name = S.model.meta.name || "Без названия";
      const t = $("#appbar-title");
      if (t) t.textContent = name;
      document.title = "Animated Diagrams — " + name; // заголовок окна (Electron) / вкладки
    },

    /* --- действия хедера (вызываются из js/menu.js) -------------------- */
    newDiagram() {
      if (confirm("Создать новую пустую диаграмму? Несохранённые изменения будут потеряны (сохраните через «Сохранить JSON»).")) {
        S.reset();
        AD.diagram.render();
        AD.diagram.resetView();
        this.setProjName();
        this.toast("Создана новая диаграмма");
      }
    },
    loadSample() {
      S.replace(AD.sampleModel());
      AD.diagram.render();
      AD.diagram.fitView();
      AD.engine.seek(0);
      this.setProjName();
      this.toast("Загружён пример flow");
    },
    openProject() {
      $("#file-input").click(); // в Electron переопределяется на нативный диалог (см. electron-bridge)
    },
    renameProject() {
      const name = prompt("Название диаграммы:", S.model.meta.name || "");
      if (name != null) {
        S.model.meta.name = name;
        S.save();
        this.setProjName();
      }
    },

    /* ------------------------------------------------------------------ */
    bindTopbar() {
      // скрытый file-input для импорта JSON (браузер)
      $("#file-input").addEventListener("change", (e) => {
        const file = e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = () => {
          try {
            S.importJSON(reader.result);
            AD.diagram.render();
            AD.diagram.fitView();
            AD.engine.seek(0);
            this.setProjName();
            this.toast("Диаграмма импортирована");
          } catch (err) {
            alert("Ошибка импорта: " + err.message);
          }
        };
        reader.readAsText(file);
        e.target.value = "";
      });
    },

    exportFile() {
      const blob = new Blob([S.exportJSON()], { type: "application/json" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = (S.model.meta.name || "diagram").replace(/\s+/g, "_") + ".json";
      a.click();
      URL.revokeObjectURL(url);
    },

    /* ------------------------------------------------------------------ */
    bindTools() {
      document.querySelectorAll("[data-tool]").forEach((btn) => {
        btn.onclick = () => AD.diagram.setTool(btn.dataset.tool);
      });
      document.querySelectorAll("[data-kind]").forEach((btn) => {
        btn.onclick = () => {
          AD.diagram.pendingKind = btn.dataset.kind;
          AD.diagram.setTool("node");
          document.querySelectorAll("[data-kind]").forEach((b) => b.classList.remove("active"));
          btn.classList.add("active");
        };
      });
      $("#btn-delete").onclick = () => this.deleteSelected();
      $("#btn-zoom-in").onclick = () => AD.diagram.zoomBy(1.2);
      $("#btn-zoom-out").onclick = () => AD.diagram.zoomBy(1 / 1.2);
      $("#btn-zoom-fit").onclick = () => AD.diagram.fitView();
    },
    reflectTool(tool) {
      document.querySelectorAll("[data-tool]").forEach((b) =>
        b.classList.toggle("active", b.dataset.tool === tool)
      );
      const hint = {
        select: "Выбор: клик — выделить, тащить — двигать. Двойной клик — переименовать.",
        node: "Узел: кликните на холст, чтобы добавить сервис выбранного типа.",
        edge: "Связь: кликните первый узел, затем второй — создастся стрелка.",
      };
      $("#canvas-hint").textContent = hint[tool] || "";
    },
    deleteSelected() {
      const sel = S.selection;
      if (!sel.id) return;
      if (sel.type === "node") S.removeNode(sel.id);
      else if (sel.type === "edge") S.removeEdge(sel.id);
      else if (sel.type === "step") S.removeStep(sel.id);
      S.clearSelection();
      AD.diagram.render();
      AD.engine.renderAt(AD.engine.t);
    },

    /* ------------------------------------------------------------------ */
    bindPlayback() {
      $("#btn-play").onclick = () => AD.engine.toggle();
      $("#btn-stop").onclick = () => AD.engine.stop();
      $("#btn-to-start").onclick = () => AD.engine.seek(0);
      $("#btn-to-end").onclick = () => AD.engine.seek(AD.engine.duration());
      $("#speed").onchange = (e) => AD.engine.setSpeed(parseFloat(e.target.value));
      $("#loop").onchange = (e) => (AD.engine.loop = e.target.checked);
      $("#duration").onchange = (e) => {
        const v = Math.max(1, parseFloat(e.target.value)) * 1000;
        S.model.scenario.duration = v;
        S._userDuration = true;
        S.touch();
        AD.timeline.render();
      };
    },
    reflectPlay(playing) {
      $("#btn-play").textContent = playing ? "⏸" : "▶";
      $("#btn-play").classList.toggle("playing", playing);
      document.body.classList.toggle("is-playing", playing);
    },
    reflectTime() {
      const t = AD.engine.t, d = AD.engine.duration();
      $("#time-readout").textContent = U.fmtTime(t) + " / " + U.fmtTime(d);
      $("#duration").value = (d / 1000).toFixed(1);
    },

    /* ------------------------------------------------------------------ */
    bindDockResize() {
      const dock = $("#timeline");
      const handle = $("#dock-resize");
      if (!handle) return;
      let drag = null;
      handle.addEventListener("mousedown", (e) => {
        drag = { y: e.clientY, h: dock.getBoundingClientRect().height };
        document.body.style.userSelect = "none";
        e.preventDefault();
      });
      window.addEventListener("mousemove", (e) => {
        if (!drag) return;
        const h = Math.max(150, Math.min(660, drag.h + (drag.y - e.clientY)));
        dock.style.height = h + "px";
      });
      window.addEventListener("mouseup", () => {
        if (drag) document.body.style.userSelect = "";
        drag = null;
      });
    },

    /* ------------------------------------------------------------------ */
    bindStepAdder() {
      const typeSel = $("#new-step-type");
      Object.keys(AD.STEP_TYPES).forEach((k) => {
        const o = document.createElement("option");
        o.value = k;
        o.textContent = AD.STEP_TYPES[k];
        typeSel.appendChild(o);
      });
      $("#btn-add-step").onclick = () => {
        const type = typeSel.value;
        const nodes = S.model.nodes;
        const at = Math.round(AD.engine.t / 50) * 50;
        let step = { type, start: at, duration: 1000 };
        if (type === "message") {
          step.from = nodes[0] ? nodes[0].id : null;
          step.to = nodes[1] ? nodes[1].id : nodes[0] ? nodes[0].id : null;
          const ed = step.from && step.to && S.edgeBetween(step.from, step.to);
          if (ed) { step.edgeId = ed.id; step.from = ed.from; step.to = ed.to; }
          step.variant = "request";
          step.label = "";
          step.duration = 1100;
        } else if (type === "timer") {
          step.nodeId = nodes[0] ? nodes[0].id : null;
          step.seconds = 5;
          step.unit = "s";
          step.duration = 5000;
          step.label = "timeout";
        } else if (type === "state") {
          step.nodeId = nodes[0] ? nodes[0].id : null;
          step.state = "down";
          step.duration = 3000;
        } else if (type === "note") {
          step.text = "Заметка";
          step.x = 60;
          step.y = 30;
          step.duration = 2500;
        } else if (type === "pulse") {
          step.nodeId = nodes[0] ? nodes[0].id : null;
          step.duration = 700;
        }
        if ((type === "message" && !step.from) || (["timer", "state", "pulse"].includes(type) && !step.nodeId)) {
          this.toast("Сначала добавьте узлы на диаграмму", true);
          return;
        }
        const s = S.addStep(step);
        S.select("step", s.id);
        AD.diagram.render();
        AD.engine.renderAt(AD.engine.t);
        AD.timeline.render();
      };
    },

    /* ------------------------------------------------------------------ */
    /* Инспектор                                                          */
    renderInspector(force) {
      if (force === undefined) force = true;
      const box = $("#inspector-body");
      // Не пересобирать инспектор, пока пользователь печатает в его поле —
      // иначе фокус слетает после первого символа. Пересборку делаем только при
      // смене выделения (force=true) или когда фокус вне полей инспектора.
      if (!force) {
        const ae = document.activeElement;
        if (ae && box.contains(ae) && /^(INPUT|SELECT|TEXTAREA)$/.test(ae.tagName)) return;
      }
      const sel = S.selection;
      box.innerHTML = "";
      if (!sel.id) {
        box.innerHTML =
          '<p class="muted">Ничего не выбрано.<br>Выберите узел, связь или шаг сценария, чтобы редактировать свойства.</p>';
        $("#inspector-title").textContent = "Свойства";
        return;
      }
      if (sel.type === "node") return this.inspectNode(box, S.node(sel.id));
      if (sel.type === "edge") return this.inspectEdge(box, S.edge(sel.id));
      if (sel.type === "step") return this.inspectStep(box, S.step(sel.id));
    },

    field(label, control) {
      const wrap = document.createElement("label");
      wrap.className = "field";
      const span = document.createElement("span");
      span.textContent = label;
      wrap.appendChild(span);
      wrap.appendChild(control);
      return wrap;
    },
    input(value, oninput, type) {
      const i = document.createElement("input");
      i.type = type || "text";
      i.value = value == null ? "" : value;
      i.oninput = () => oninput(type === "number" ? parseFloat(i.value) : i.value);
      return i;
    },
    selectCtl(value, options, onchange) {
      const s = document.createElement("select");
      options.forEach((o) => {
        const opt = document.createElement("option");
        opt.value = o.value;
        opt.textContent = o.label;
        if (o.value === value) opt.selected = true;
        s.appendChild(opt);
      });
      s.onchange = () => onchange(s.value);
      return s;
    },
    nodeOptions() {
      return S.model.nodes.map((n) => ({ value: n.id, label: n.label }));
    },
    edgeOptions() {
      const nm = (id) => (S.node(id) ? S.node(id).label : "?");
      const seen = {};
      return S.model.edges.map((e) => {
        const key = e.from + ">" + e.to;
        seen[key] = (seen[key] || 0) + 1;
        const suffix = seen[key] > 1 ? " (" + seen[key] + ")" : "";
        return { value: e.id, label: `${nm(e.from)} → ${nm(e.to)}${suffix}${e.label ? " · " + e.label : ""}` };
      });
    },
    portOptions(nodeId) {
      const n = S.node(nodeId);
      const opts = [{ value: "", label: "Авто" }];
      (n && n.ports ? n.ports : []).forEach((p, i) => opts.push({ value: p.id, label: "Точка " + (i + 1) }));
      return opts;
    },
    checkbox(label, checked, onchange) {
      const wrap = document.createElement("label");
      wrap.className = "field-check";
      const cb = document.createElement("input");
      cb.type = "checkbox";
      cb.checked = !!checked;
      cb.onchange = () => onchange(cb.checked);
      const span = document.createElement("span");
      span.textContent = label;
      wrap.appendChild(cb);
      wrap.appendChild(span);
      return wrap;
    },
    delBtn(fn) {
      const b = document.createElement("button");
      b.className = "btn danger full";
      b.textContent = "Удалить";
      b.onclick = fn;
      return b;
    },
    commit(rerenderTimeline) {
      S.touch();
      AD.diagram.render();
      AD.engine.renderAt(AD.engine.t);
      if (rerenderTimeline) AD.timeline.render();
    },

    inspectNode(box, n) {
      if (!n) return;
      $("#inspector-title").textContent = "Узел";
      box.appendChild(this.field("Название", this.input(n.label, (v) => { n.label = v; this.commit(true); })));
      box.appendChild(this.field("Подзаголовок", this.input(n.subtitle || "", (v) => { n.subtitle = v; this.commit(); })));
      box.appendChild(this.field("Тип", this.selectCtl(n.kind,
        Object.keys(AD.NODE_KINDS).map((k) => ({ value: k, label: AD.NODE_KINDS[k].label })),
        (v) => { n.kind = v; n.shape = AD.NODE_KINDS[v].shape; this.commit(); })));
      box.appendChild(this.field("Цвет акцента", this.input(n.color, (v) => { n.color = v; this.commit(); }, "color")));
      const size = document.createElement("div");
      size.className = "row2";
      size.appendChild(this.field("Ширина", this.input(n.w, (v) => { n.w = v || 140; this.commit(true); }, "number")));
      size.appendChild(this.field("Высота", this.input(n.h, (v) => { n.h = v || 64; this.commit(true); }, "number")));
      box.appendChild(size);

      // точки соединения (порты)
      const head = document.createElement("div");
      head.className = "sub-head";
      head.textContent = "Точки соединения";
      box.appendChild(head);
      (n.ports || []).forEach((p, i) => {
        const row = document.createElement("div");
        row.className = "port-row";
        const lbl = document.createElement("span");
        lbl.textContent = `Точка ${i + 1}`;
        const rm = document.createElement("button");
        rm.className = "btn danger small";
        rm.textContent = "✕";
        rm.onclick = () => { S.removePort(n.id, p.id); this.commit(true); this.renderInspector(); };
        row.appendChild(lbl);
        row.appendChild(rm);
        box.appendChild(row);
      });
      const addPort = document.createElement("button");
      addPort.className = "btn full";
      addPort.textContent = "＋ Добавить точку соединения";
      addPort.onclick = () => { S.addPort(n.id); this.commit(true); this.renderInspector(); };
      box.appendChild(addPort);
      const portHint = document.createElement("p");
      portHint.className = "hint";
      portHint.textContent = "Точки можно перетаскивать. В режиме «Связь» кликните по точке, чтобы привязать к ней связь.";
      box.appendChild(portHint);

      box.appendChild(this.delBtn(() => this.deleteSelected()));
    },

    inspectEdge(box, e) {
      if (!e) return;
      $("#inspector-title").textContent = "Связь";
      const nm = (id) => (S.node(id) ? S.node(id).label : "?");
      const info = document.createElement("p");
      info.className = "muted";
      info.textContent = `${nm(e.from)} → ${nm(e.to)}`;
      box.appendChild(info);
      box.appendChild(this.field("Подпись", this.input(e.label, (v) => { e.label = v; this.commit(); })));
      box.appendChild(this.field("Стиль", this.selectCtl(e.style,
        [{ value: "solid", label: "Сплошная" }, { value: "dashed", label: "Пунктир" }],
        (v) => { e.style = v; this.commit(); })));

      // привязка к точкам соединения (портам)
      const prow = document.createElement("div");
      prow.className = "row2";
      prow.appendChild(this.field("Вход (от)", this.selectCtl(e.fromPort || "", this.portOptions(e.from), (v) => { e.fromPort = v || null; this.commit(); })));
      prow.appendChild(this.field("Выход (к)", this.selectCtl(e.toPort || "", this.portOptions(e.to), (v) => { e.toPort = v || null; this.commit(); })));
      box.appendChild(prow);

      const hasWp = e.waypoints && e.waypoints.length;
      const curve = document.createElement("input");
      curve.type = "range"; curve.min = "-0.5"; curve.max = "0.5"; curve.step = "0.05"; curve.value = e.curve || 0;
      curve.disabled = !!hasWp;
      curve.oninput = () => { e.curve = parseFloat(curve.value); this.commit(); };
      box.appendChild(this.field("Изгиб" + (hasWp ? " (задан точками)" : ""), curve));

      box.appendChild(this.checkbox("Двунаправленная (стрелки с обеих сторон)", e.bidirectional, (v) => { e.bidirectional = v; this.commit(); }));

      // точки изгиба
      const wpRow = document.createElement("div");
      wpRow.className = "port-row";
      const wpLbl = document.createElement("span");
      wpLbl.textContent = `Точек изгиба: ${(e.waypoints || []).length}`;
      const wpClear = document.createElement("button");
      wpClear.className = "btn small";
      wpClear.textContent = "Очистить";
      wpClear.onclick = () => { e.waypoints = []; this.commit(); this.renderInspector(); };
      wpRow.appendChild(wpLbl);
      wpRow.appendChild(wpClear);
      box.appendChild(wpRow);
      const wpHint = document.createElement("p");
      wpHint.className = "hint";
      wpHint.textContent = "Двойной клик по связи — добавить точку изгиба; по точке — удалить. Точки перетаскиваются.";
      box.appendChild(wpHint);

      const flip = document.createElement("button");
      flip.className = "btn full";
      flip.textContent = "⇄ Развернуть направление";
      flip.onclick = () => {
        const t = e.from; e.from = e.to; e.to = t;
        const tp = e.fromPort; e.fromPort = e.toPort; e.toPort = tp;
        e.curve = -(e.curve || 0);
        if (e.waypoints) e.waypoints.reverse();
        this.commit(); this.renderInspector();
      };
      box.appendChild(flip);
      box.appendChild(this.delBtn(() => this.deleteSelected()));
    },

    inspectStep(box, s) {
      if (!s) return;
      $("#inspector-title").textContent = "Шаг: " + AD.STEP_TYPES[s.type];

      box.appendChild(this.field("Тип шага", this.selectCtl(s.type,
        Object.keys(AD.STEP_TYPES).map((k) => ({ value: k, label: AD.STEP_TYPES[k] })),
        (v) => { s.type = v; this.normalizeStep(s); this.commit(true); this.renderInspector(); })));

      if (s.type === "message") {
        // сообщение летит по выбранной связи
        const edgeOpts = [{ value: "", label: "— по узлам (без связи) —" }].concat(this.edgeOptions());
        box.appendChild(this.field("Связь", this.selectCtl(s.edgeId || "", edgeOpts, (v) => {
          s.edgeId = v || null;
          const e = v && S.edge(v);
          if (e) { s.from = e.from; s.to = e.to; }
          this.commit(true); this.renderInspector();
        })));
        const e = s.edgeId && S.edge(s.edgeId);
        if (e) {
          const nm = (id) => (S.node(id) ? S.node(id).label : "?");
          const dir = document.createElement("button");
          dir.className = "btn full";
          dir.textContent = `Направление: ${nm(s.from)} → ${nm(s.to)}  ⇄`;
          dir.onclick = () => { const t = s.from; s.from = s.to; s.to = t; this.commit(true); this.renderInspector(); };
          box.appendChild(dir);
        } else {
          box.appendChild(this.field("От", this.selectCtl(s.from, this.nodeOptions(), (v) => { s.from = v; this.commit(true); })));
          box.appendChild(this.field("К", this.selectCtl(s.to, this.nodeOptions(), (v) => { s.to = v; this.commit(true); })));
        }
        box.appendChild(this.field("Вариант", this.selectCtl(s.variant,
          Object.keys(AD.MSG_VARIANTS).map((k) => ({ value: k, label: AD.MSG_VARIANTS[k].label })),
          (v) => { s.variant = v; this.commit(true); })));
        box.appendChild(this.field("Подпись", this.input(s.label, (v) => { s.label = v; this.commit(true); })));
      } else if (s.type === "timer") {
        box.appendChild(this.field("Узел", this.selectCtl(s.nodeId, this.nodeOptions(), (v) => { s.nodeId = v; this.commit(true); })));
        const trow = document.createElement("div");
        trow.className = "row2";
        trow.appendChild(this.field("Отсчёт от", this.input(s.seconds, (v) => { s.seconds = v; this.commit(true); }, "number")));
        trow.appendChild(this.field("Единица", this.selectCtl(s.unit || "s",
          Object.keys(AD.TIME_UNITS).map((k) => ({ value: k, label: AD.TIME_UNITS[k].label })),
          (v) => { s.unit = v; this.commit(true); })));
        box.appendChild(trow);
        box.appendChild(this.field("Подпись", this.input(s.label, (v) => { s.label = v; this.commit(true); })));
      } else if (s.type === "state") {
        box.appendChild(this.field("Узел", this.selectCtl(s.nodeId, this.nodeOptions(), (v) => { s.nodeId = v; this.commit(true); })));
        box.appendChild(this.field("Состояние", this.selectCtl(s.state,
          Object.keys(AD.NODE_STATES).map((k) => ({ value: k, label: AD.NODE_STATES[k].label })),
          (v) => { s.state = v; this.commit(true); })));
      } else if (s.type === "note") {
        box.appendChild(this.field("Текст", this.input(s.text, (v) => { s.text = v; this.commit(true); })));
        const pos = document.createElement("div");
        pos.className = "row2";
        pos.appendChild(this.field("X", this.input(s.x, (v) => { s.x = v; this.commit(); }, "number")));
        pos.appendChild(this.field("Y", this.input(s.y, (v) => { s.y = v; this.commit(); }, "number")));
        box.appendChild(pos);
        box.appendChild(this.field("Цвет", this.input(s.color || "#fbbf24", (v) => { s.color = v; this.commit(); }, "color")));
      } else if (s.type === "pulse") {
        box.appendChild(this.field("Узел", this.selectCtl(s.nodeId, this.nodeOptions(), (v) => { s.nodeId = v; this.commit(true); })));
        box.appendChild(this.field("Цвет", this.input(s.color || "#22d3ee", (v) => { s.color = v; this.commit(); }, "color")));
      }

      const timing = document.createElement("div");
      timing.className = "row2";
      timing.appendChild(this.field("Начало, мс", this.input(s.start, (v) => { s.start = Math.max(0, v || 0); S.sortSteps(); this.commit(true); }, "number")));
      timing.appendChild(this.field("Длит., мс", this.input(s.duration, (v) => { s.duration = Math.max(100, v || 100); this.commit(true); }, "number")));
      box.appendChild(timing);

      const dup = document.createElement("button");
      dup.className = "btn full";
      dup.textContent = "⧉ Дублировать шаг";
      dup.onclick = () => {
        const copy = U.deepClone(s);
        delete copy.id;
        copy.start = s.start + s.duration + 200;
        const ns = S.addStep(copy);
        S.select("step", ns.id);
        this.commit(true);
      };
      box.appendChild(dup);
      box.appendChild(this.delBtn(() => this.deleteSelected()));
    },

    normalizeStep(s) {
      const nodes = S.model.nodes;
      if (s.type === "message") {
        s.from = s.from || (s.nodeId || (nodes[0] && nodes[0].id));
        s.to = s.to || (nodes[1] && nodes[1].id) || s.from;
        s.variant = s.variant || "request";
        if (!s.edgeId) { const ed = S.edgeBetween(s.from, s.to); if (ed) s.edgeId = ed.id; }
      } else if (s.type === "timer") {
        s.nodeId = s.nodeId || s.from || (nodes[0] && nodes[0].id);
        s.seconds = s.seconds || Math.round(s.duration / 1000);
        s.unit = s.unit || "s";
      } else if (s.type === "state") {
        s.nodeId = s.nodeId || s.from || (nodes[0] && nodes[0].id);
        s.state = s.state || "down";
      } else if (s.type === "pulse") {
        s.nodeId = s.nodeId || s.from || (nodes[0] && nodes[0].id);
      } else if (s.type === "note") {
        s.text = s.text || "Заметка";
        s.x = s.x || 60; s.y = s.y || 30;
      }
    },

    /* ------------------------------------------------------------------ */
    bindKeys() {
      window.addEventListener("keydown", (e) => {
        if (e.code === "Space") AD._space = true;
        const typing = /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName);
        if (typing) return;
        if (e.code === "Space") { e.preventDefault(); AD.engine.toggle(); }
        else if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); this.deleteSelected(); }
        else if (e.key === "v" || e.key === "V") AD.diagram.setTool("select");
        else if (e.key === "n" || e.key === "N") AD.diagram.setTool("node");
        else if (e.key === "e" || e.key === "E") AD.diagram.setTool("edge");
        else if (e.key === "Escape") { S.clearSelection(); AD.diagram._connectFrom = null; AD.diagram.render(); }
      });
      window.addEventListener("keyup", (e) => {
        if (e.code === "Space") AD._space = false;
      });
    },

    toast(msg, warn) {
      const t = $("#toast");
      t.textContent = msg;
      t.className = "toast show" + (warn ? " warn" : "");
      clearTimeout(this._toastT);
      this._toastT = setTimeout(() => (t.className = "toast"), 2400);
    },
  });

  window.addEventListener("DOMContentLoaded", () => App.init());
})();
