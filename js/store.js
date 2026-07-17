/* =============================================================================
 * store.js — модель данных, персистентность, утилиты, образец сценария
 * =========================================================================== */
(function () {
  "use strict";
  const AD = (window.AD = window.AD || {});

  /* ---- утилиты ------------------------------------------------------------ */
  const U = (AD.util = {
    id(prefix) {
      return (prefix || "id") + "_" + Math.random().toString(36).slice(2, 9);
    },
    clamp(v, a, b) {
      return Math.max(a, Math.min(b, v));
    },
    lerp(a, b, t) {
      return a + (b - a) * t;
    },
    /** плавная кривая ease-in-out */
    easeInOut(t) {
      return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
    },
    deepClone(obj) {
      return JSON.parse(JSON.stringify(obj));
    },
    fmtTime(ms) {
      const s = ms / 1000;
      return s.toFixed(s < 10 ? 2 : 1) + "с";
    },
    emit(name, detail) {
      window.dispatchEvent(new CustomEvent(name, { detail }));
    },
    on(name, fn) {
      window.addEventListener(name, fn);
    },
  });

  /* ---- каталог типов узлов ------------------------------------------------ */
  AD.NODE_KINDS = {
    service: { label: "Сервис", color: "#4f8cff", shape: "round", icon: "▢" },
    client: { label: "Клиент", color: "#22c55e", shape: "round", icon: "◎" },
    db: { label: "БД", color: "#a855f7", shape: "db", icon: "◫" },
    queue: { label: "Очередь", color: "#f59e0b", shape: "queue", icon: "≣" },
    gateway: { label: "Шлюз", color: "#06b6d4", shape: "round", icon: "◇" },
    external: { label: "Внешний", color: "#94a3b8", shape: "round", icon: "◈" },
  };

  /* ---- каталог состояний узла --------------------------------------------- */
  AD.NODE_STATES = {
    ok: { label: "Норма", color: "#3b4a63", ring: "#5a6b86" },
    active: { label: "Активен", color: "#1d4ed8", ring: "#60a5fa" },
    busy: { label: "Занят", color: "#a16207", ring: "#f59e0b" },
    warn: { label: "Предупреждение", color: "#b45309", ring: "#fb923c" },
    down: { label: "Недоступен", color: "#7f1d1d", ring: "#ef4444" },
    success: { label: "Успех", color: "#166534", ring: "#22c55e" },
  };

  /* Разрешение состояния узла в набор параметров отрисовки: пресет + переопределения
     шага (своя подпись, цвет-акцент, размер шрифта). Тёмная заливка выводится из
     акцента библиотекой d3 (без «магии»). step может быть null (состояние по умолчанию). */
  AD.resolveNodeState = function (step) {
    if (!step) {
      const b = AD.NODE_STATES.ok;
      return { id: "ok", label: b.label, fill: b.color, ring: b.ring, size: null };
    }
    const base = AD.NODE_STATES[step.state] || AD.NODE_STATES.ok;
    const ring = step.color || base.ring;
    let fill = base.color;
    if (step.color) {
      try { fill = String(window.d3.color(step.color).darker(1.9)); } catch (e) { fill = step.color; }
    }
    const label = step.label != null && step.label !== "" ? step.label : base.label;
    return { id: step.state, label, fill, ring, size: step.labelSize || null };
  };

  /* ---- варианты сообщений ------------------------------------------------- */
  AD.MSG_VARIANTS = {
    request: { label: "Запрос", color: "#60a5fa", dash: null },
    response: { label: "Ответ", color: "#34d399", dash: null },
    retry: { label: "Ретрай", color: "#fbbf24", dash: "6 5" },
    error: { label: "Ошибка", color: "#f87171", dash: "2 5" },
    success: { label: "Успех", color: "#4ade80", dash: null },
    event: { label: "Событие", color: "#c084fc", dash: "1 6" },
  };

  /* ---- анимации связи для шага «Соединение» ------------------------------- */
  AD.LINK_ANIMS = {
    flow: { label: "Бегущий пунктир", dash: "7 6", flow: true, pulse: false },
    dash: { label: "Пунктир", dash: "7 6", flow: false, pulse: false },
    solid: { label: "Сплошная", dash: null, flow: false, pulse: false },
    pulse: { label: "Пульсация", dash: null, flow: false, pulse: true },
  };

  AD.STEP_TYPES = {
    message: "Сообщение",
    timer: "Таймер",
    state: "Смена состояния",
    action: "Действие",
    link: "Соединение",
    note: "Заметка",
    pulse: "Пульс/подсветка",
  };

  /* ---- единицы времени для отображения таймера ---------------------------- */
  AD.TIME_UNITS = {
    s: { label: "Секунды", short: "с" },
    m: { label: "Минуты", short: "мин" },
    h: { label: "Часы", short: "ч" },
    d: { label: "Дни", short: "дн" },
  };

  /* ---- модель ------------------------------------------------------------- */
  const STORAGE_KEY = "animated_diagrams_project_v1";

  function emptyModel() {
    return {
      version: 1,
      meta: { name: "Новая диаграмма", createdAt: Date.now() },
      view: { zoom: 1, panX: 0, panY: 0 },
      nodes: [],
      edges: [],
      scenario: { duration: 12000, steps: [] },
    };
  }

  const Store = (AD.store = {
    model: emptyModel(),
    selection: { type: null, id: null }, // node|edge|step

    /* --- доступ к элементам --- */
    node(id) {
      return this.model.nodes.find((n) => n.id === id);
    },
    edge(id) {
      return this.model.edges.find((e) => e.id === id);
    },
    step(id) {
      return this.model.scenario.steps.find((s) => s.id === id);
    },
    edgeBetween(a, b) {
      return this.model.edges.find(
        (e) => (e.from === a && e.to === b) || (e.from === b && e.to === a)
      );
    },

    /* --- мутации --- */
    addNode(x, y, kind) {
      kind = kind || "service";
      const k = AD.NODE_KINDS[kind];
      const n = {
        id: U.id("n"),
        label: k.label + " " + (this.model.nodes.length + 1),
        kind,
        x: Math.round(x - 70),
        y: Math.round(y - 32),
        w: 140,
        h: 64,
        color: k.color,
        shape: k.shape,
        subtitle: "",
      };
      this.model.nodes.push(n);
      this.touch();
      return n;
    },
    addEdge(from, to, fromPort, toPort) {
      if (from === to) return null;
      // разрешаем несколько связей между теми же узлами; авто-смещаем изгиб,
      // чтобы параллельные связи не сливались в одну линию
      const dup = this.model.edges.filter(
        (e) => (e.from === from && e.to === to) || (e.from === to && e.to === from)
      ).length;
      const curve = dup === 0 ? 0 : (dup % 2 ? 1 : -1) * 0.2 * Math.ceil(dup / 2);
      const e = {
        id: U.id("e"),
        from,
        to,
        fromPort: fromPort || null,
        toPort: toPort || null,
        label: "",
        style: "solid",
        curve,
        waypoints: [],
        bidirectional: false,
      };
      this.model.edges.push(e);
      this.touch();
      return e;
    },
    removeNode(id) {
      const goneEdges = this.model.edges.filter((e) => e.from === id || e.to === id).map((e) => e.id);
      this.model.nodes = this.model.nodes.filter((n) => n.id !== id);
      this.model.edges = this.model.edges.filter(
        (e) => e.from !== id && e.to !== id
      );
      this.model.scenario.steps = this.model.scenario.steps.filter(
        (s) => s.nodeId !== id && s.from !== id && s.to !== id
      );
      for (const s of this.model.scenario.steps) {
        if (s.type === "message" && goneEdges.includes(s.edgeId)) s.edgeId = null;
      }
      this.touch();
    },
    removeEdge(id) {
      this.model.edges = this.model.edges.filter((e) => e.id !== id);
      for (const s of this.model.scenario.steps) {
        if (s.type === "message" && s.edgeId === id) s.edgeId = null;
      }
      this.touch();
    },

    /* --- порты (точки соединения) --- */
    port(nodeId, portId) {
      const n = this.node(nodeId);
      return n && n.ports ? n.ports.find((p) => p.id === portId) : null;
    },
    addPort(nodeId, dx, dy) {
      const n = this.node(nodeId);
      if (!n) return null;
      n.ports = n.ports || [];
      const p = { id: U.id("p"), dx: dx == null ? n.w : Math.round(dx), dy: dy == null ? n.h / 2 : Math.round(dy) };
      n.ports.push(p);
      this.touch();
      return p;
    },
    removePort(nodeId, portId) {
      const n = this.node(nodeId);
      if (!n || !n.ports) return;
      n.ports = n.ports.filter((p) => p.id !== portId);
      for (const e of this.model.edges) {
        if (e.fromPort === portId) e.fromPort = null;
        if (e.toPort === portId) e.toPort = null;
      }
      this.touch();
    },

    /* --- точки изгиба связи (waypoints) --- */
    addWaypoint(edgeId, x, y, index) {
      const e = this.edge(edgeId);
      if (!e) return;
      e.waypoints = e.waypoints || [];
      const wp = { x: Math.round(x), y: Math.round(y) };
      if (index == null || index < 0 || index > e.waypoints.length) e.waypoints.push(wp);
      else e.waypoints.splice(index, 0, wp);
      this.touch();
    },
    removeWaypoint(edgeId, index) {
      const e = this.edge(edgeId);
      if (!e || !e.waypoints) return;
      e.waypoints.splice(index, 1);
      this.touch();
    },
    addStep(step) {
      const s = Object.assign(
        {
          id: U.id("s"),
          type: "message",
          start: 0,
          duration: 1200,
        },
        step
      );
      this.model.scenario.steps.push(s);
      this.sortSteps();
      this.autoDuration();
      this.touch();
      return s;
    },
    removeStep(id) {
      this.model.scenario.steps = this.model.scenario.steps.filter(
        (s) => s.id !== id
      );
      this.touch();
    },
    sortSteps() {
      this.model.scenario.steps.sort((a, b) => a.start - b.start);
    },
    autoDuration() {
      let max = 0;
      for (const s of this.model.scenario.steps) {
        max = Math.max(max, s.start + s.duration);
      }
      this.model.scenario.duration = Math.max(
        4000,
        Math.ceil((max + 1200) / 500) * 500,
        this.model.scenario.duration && this._userDuration
          ? this.model.scenario.duration
          : 0
      );
    },

    select(type, id) {
      this.selection = { type, id };
      U.emit("ad:selection", this.selection);
    },
    clearSelection() {
      this.select(null, null);
    },

    /* --- персистентность --- */
    touch() {
      this.save();
      U.emit("ad:model");
    },
    save() {
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(this.model));
      } catch (e) {
        /* приватный режим и т.п. */
      }
    },
    load() {
      try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (raw) {
          this.model = JSON.parse(raw);
          return true;
        }
      } catch (e) {}
      return false;
    },
    replace(model) {
      this.model = model;
      this.clearSelection();
      this.touch();
    },
    reset() {
      this.replace(emptyModel());
    },
    exportJSON() {
      return JSON.stringify(this.model, null, 2);
    },
    importJSON(text) {
      const m = JSON.parse(text);
      if (!m.nodes || !m.scenario) throw new Error("Некорректный формат файла");
      this.replace(m);
    },
  });

  /* ---- образец: пример flow из ТЗ ----------------------------------------- */
  AD.sampleModel = function () {
    const A = { id: "svcA", label: "Сервис A", kind: "service", x: 80, y: 120, w: 150, h: 66, color: AD.NODE_KINDS.service.color, shape: "round" };
    const B = { id: "svcB", label: "Сервис B", kind: "service", x: 480, y: 60, w: 150, h: 66, color: AD.NODE_KINDS.service.color, shape: "round" };
    const C = { id: "svcC", label: "Сервис C", kind: "service", x: 480, y: 260, w: 150, h: 66, color: AD.NODE_KINDS.service.color, shape: "round" };
    return {
      version: 1,
      meta: { name: "Пример: fallback с ретраями и таймаутом", createdAt: Date.now() },
      view: { zoom: 1, panX: 0, panY: 0 },
      nodes: [A, B, C],
      edges: [
        { id: "eAB", from: "svcA", to: "svcB", label: "REST", style: "solid", curve: 0.15 },
        { id: "eAC", from: "svcA", to: "svcC", label: "fallback", style: "dashed", curve: -0.15 },
      ],
      scenario: {
        duration: 12000,
        steps: [
          // 1. A -> B: первичный запрос
          { id: "s1", type: "message", from: "svcA", to: "svcB", variant: "request", label: "запрос", start: 300, duration: 1100 },
          // 2. B недоступен (держим до конца сценария)
          { id: "s2", type: "state", nodeId: "svcB", state: "down", start: 1400, duration: 10600 },
          { id: "s2b", type: "note", text: "Сервис B недоступен", x: 470, y: 20, start: 1500, duration: 5100 },
          // 3. A запускает таймер на 5с и параллельно шлёт ретраи
          { id: "s3", type: "timer", nodeId: "svcA", seconds: 5, label: "timeout", start: 1600, duration: 5000 },
          { id: "s4", type: "message", from: "svcA", to: "svcB", variant: "retry", label: "retry 1", start: 2400, duration: 900 },
          { id: "s5", type: "message", from: "svcA", to: "svcB", variant: "retry", label: "retry 2", start: 3800, duration: 900 },
          { id: "s6", type: "message", from: "svcA", to: "svcB", variant: "retry", label: "retry 3", start: 5200, duration: 900 },
          // ретраи возвращаются ошибкой
          { id: "s5e", type: "message", from: "svcB", to: "svcA", variant: "error", label: "нет ответа", start: 4750, duration: 700 },
          // 4. таймаут истёк -> A уходит на C
          { id: "s7", type: "pulse", nodeId: "svcA", start: 6600, duration: 700 },
          { id: "s7n", type: "note", text: "5с истекли — переключение на C", x: 60, y: 210, start: 6600, duration: 2200 },
          { id: "s8", type: "message", from: "svcA", to: "svcC", variant: "request", label: "запрос", start: 7000, duration: 1100 },
          { id: "s9", type: "state", nodeId: "svcC", state: "active", start: 8100, duration: 3900 },
          { id: "s10", type: "message", from: "svcC", to: "svcA", variant: "success", label: "200 OK", start: 8400, duration: 1000 },
          { id: "s11", type: "state", nodeId: "svcA", state: "success", start: 9500, duration: 2500 },
        ],
      },
    };
  };

  Store._userDuration = false;
})();
