/* =============================================================================
 * diagram.js — отрисовка и редактирование диаграммы (SVG)
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

  const Diagram = (AD.diagram = {
    svg: null,
    viewport: null, // <g> с pan/zoom
    layers: {},
    tool: "select", // select | node | edge
    pendingKind: "service",
    _connectFrom: null,
    _drag: null,
    _pan: null,
    /** внешний резолвер состояния узла во время воспроизведения */
    stateResolver: null,
    /** карта временных подсветок узлов {nodeId: {ring,scale}} */
    highlights: {},

    init(svgEl) {
      this.svg = svgEl;
      this.buildDefs();
      this.viewport = el("g", { id: "viewport" });
      this.svg.appendChild(this.viewport);
      this.layers.edges = el("g", { class: "layer-edges" });
      this.layers.nodes = el("g", { class: "layer-nodes" });
      this.layers.overlay = el("g", { class: "layer-overlay" });
      this.viewport.appendChild(this.layers.edges);
      this.viewport.appendChild(this.layers.nodes);
      this.viewport.appendChild(this.layers.overlay);
      this.bindEvents();
      this.applyView();
    },

    buildDefs() {
      const defs = el("defs");
      // стрелки для каждого варианта сообщения + базовая
      const arrows = { base: "#8aa0c0" };
      for (const k in AD.MSG_VARIANTS) arrows["arw-" + k] = AD.MSG_VARIANTS[k].color;
      for (const id in arrows) {
        // refX=0 — линия стыкуется с ОСНОВАНИЕМ стрелки, стрелка выдвигается вперёд.
        // userSpaceOnUse — размер стрелки в абсолютных единицах (см. ARROW_LEN),
        // не зависит от толщины линии, чтобы отступы были предсказуемы.
        const m = el("marker", {
          id: "arrow-" + id,
          viewBox: "0 0 12 10",
          refX: "0",
          refY: "5",
          markerWidth: "12",
          markerHeight: "10",
          markerUnits: "userSpaceOnUse",
          orient: "auto-start-reverse",
        });
        m.appendChild(el("path", { d: "M0,0 L12,5 L0,10 z", fill: arrows[id] }));
        defs.appendChild(m);
      }
      // мягкая тень
      const f = el("filter", { id: "nodeShadow", x: "-30%", y: "-30%", width: "160%", height: "160%" });
      f.innerHTML =
        '<feDropShadow dx="0" dy="3" stdDeviation="4" flood-color="#000" flood-opacity="0.35"/>';
      defs.appendChild(f);
      const glow = el("filter", { id: "glow", x: "-60%", y: "-60%", width: "220%", height: "220%" });
      glow.innerHTML =
        '<feGaussianBlur stdDeviation="4" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>';
      defs.appendChild(glow);
      this.svg.appendChild(defs);
    },

    /* --- преобразование координат экран<->мир --- */
    toWorld(clientX, clientY) {
      const r = this.svg.getBoundingClientRect();
      const v = S.model.view;
      return {
        x: (clientX - r.left - v.panX) / v.zoom,
        y: (clientY - r.top - v.panY) / v.zoom,
      };
    },
    applyView() {
      const v = S.model.view;
      this.viewport.setAttribute(
        "transform",
        `translate(${v.panX},${v.panY}) scale(${v.zoom})`
      );
    },
    setTool(tool) {
      this.tool = tool;
      this._connectFrom = null;
      this.svg.dataset.tool = tool;
      this.render();
      U.emit("ad:tool", tool);
    },
    zoomBy(factor, cx, cy) {
      const v = S.model.view;
      const r = this.svg.getBoundingClientRect();
      cx = cx == null ? r.width / 2 : cx - r.left;
      cy = cy == null ? r.height / 2 : cy - r.top;
      const wx = (cx - v.panX) / v.zoom;
      const wy = (cy - v.panY) / v.zoom;
      v.zoom = U.clamp(v.zoom * factor, 0.25, 3);
      v.panX = cx - wx * v.zoom;
      v.panY = cy - wy * v.zoom;
      this.applyView();
      S.save();
    },
    resetView() {
      S.model.view = { zoom: 1, panX: 0, panY: 0 };
      this.applyView();
      S.save();
    },
    fitView() {
      const ns = S.model.nodes;
      if (!ns.length) return this.resetView();
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
      for (const n of ns) {
        minX = Math.min(minX, n.x);
        minY = Math.min(minY, n.y);
        maxX = Math.max(maxX, n.x + n.w);
        maxY = Math.max(maxY, n.y + n.h);
      }
      const r = this.svg.getBoundingClientRect();
      const pad = 60;
      const zoom = U.clamp(
        Math.min((r.width - pad * 2) / (maxX - minX), (r.height - pad * 2) / (maxY - minY)),
        0.25, 2
      );
      S.model.view = {
        zoom,
        panX: r.width / 2 - ((minX + maxX) / 2) * zoom,
        panY: r.height / 2 - ((minY + maxY) / 2) * zoom,
      };
      this.applyView();
      S.save();
    },

    /* --- геометрия связей (вся математика — в AD.geom, пути строит d3) --- */

    /** длина маркера-стрелки (в мировых единицах, userSpaceOnUse) */
    ARROW_LEN: 12,

    /** геометрия связи с учётом портов и точек изгиба: {start,end,rawStart,rawEnd,points,d} */
    edgeGeom(edge) {
      const G = AD.geom;
      const a = S.node(edge.from), b = S.node(edge.to);
      if (!a || !b) return null;
      const wps = edge.waypoints || [];
      const ca = G.nodeCenter(a), cb = G.nodeCenter(b);
      let startRef, endRef, ctrl;
      if (wps.length) {
        startRef = wps[0];
        endRef = wps[wps.length - 1];
      } else {
        ctrl = G.perpControl(a, b, edge.curve || 0);
        startRef = ctrl;
        endRef = ctrl;
      }
      const fp = edge.fromPort && S.port(edge.from, edge.fromPort);
      const tp = edge.toPort && S.port(edge.to, edge.toPort);
      const GAP = 4; // маленький зазор между кончиком стрелки и узлом
      // «наконечники» — точки, куда должны указывать концы связи (кончики стрелок)
      const rawStart = fp ? G.portPos(a, fp)
        : G.borderPoint(ca.x, ca.y, a.w / 2 + GAP, a.h / 2 + GAP, startRef.x, startRef.y);
      const rawEnd = tp ? G.portPos(b, tp)
        : G.borderPoint(cb.x, cb.y, b.w / 2 + GAP, b.h / 2 + GAP, endRef.x, endRef.y);
      // Если на конце есть стрелка — отводим фактический конец линии назад на длину
      // стрелки: тогда ОСНОВАНИЕ стрелки стыкуется с линией, а КОНЧИК попадает в rawEnd.
      // На изгибе тело стрелки целиком впереди конечной точки, поэтому не отделяется.
      const start = edge.bidirectional ? G.pullBack(startRef, rawStart, this.ARROW_LEN) : rawStart;
      const end = G.pullBack(endRef, rawEnd, this.ARROW_LEN);
      const points = [start, ...wps, end];
      let d;
      if (wps.length) d = G.smoothPathD(points);
      else if (edge.curve) d = G.quadPathD(start, ctrl, end);
      else d = G.linePathD(start, end);
      return { start, end, rawStart, rawEnd, points, d };
    },

    edgePathEl(id) {
      return this.layers.edges.querySelector('path.edge[data-edge="' + CSS.escape(id) + '"]');
    },

    /** индекс сегмента (для вставки точки изгиба) ближайшего к точке w */
    nearestSegmentIndex(edgeId, w) {
      const geom = this.edgeGeom(S.edge(edgeId));
      if (!geom) return 0;
      const pts = geom.points;
      let best = 0, bestD = Infinity;
      for (let k = 0; k < pts.length - 1; k++) {
        const d = AD.geom.distToSegment(w, pts[k], pts[k + 1]);
        if (d < bestD) { bestD = d; best = k; }
      }
      return best;
    },

    /** дескриптор состояния узла {id,label,fill,ring,size} с учётом переопределений шага */
    effectiveState(nodeId) {
      const step = this.stateResolver ? this.stateResolver(nodeId) : null;
      return AD.resolveNodeState(step);
    },

    /* --- отрисовка --- */
    render() {
      this.renderEdges();
      this.renderNodes();
    },

    renderEdges() {
      const g = this.layers.edges;
      g.innerHTML = "";
      for (const e of S.model.edges) {
        const geom = this.edgeGeom(e);
        if (!geom) continue;
        const selected = S.selection.type === "edge" && S.selection.id === e.id;
        const p = el("path", {
          d: geom.d,
          class: "edge" + (selected ? " selected" : ""),
          fill: "none",
          "stroke-dasharray": e.style === "dashed" ? "7 6" : "",
          "marker-end": "url(#arrow-base)",
          "data-edge": e.id,
        });
        if (e.bidirectional) p.setAttribute("marker-start", "url(#arrow-base)");
        g.appendChild(p);
        // невидимая широкая линия для удобного клика
        const hit = el("path", { d: geom.d, class: "edge-hit", "data-edge": e.id, fill: "none" });
        g.appendChild(hit);
        if (e.label) {
          const pos = e.labelPos != null ? e.labelPos : 0.5;
          const off = e.labelOff != null ? e.labelOff : 10;
          const pt = AD.geom.pointAlongPath(p, pos, off);
          const t = el("text", { x: pt.x, y: pt.y, class: "edge-label", "text-anchor": "middle", "dominant-baseline": "middle" });
          if (e.labelSize) t.style.fontSize = e.labelSize + "px";
          t.textContent = e.label;
          g.appendChild(t);
        }
        // ручки точек изгиба — только у выбранной связи
        if (selected && e.waypoints && e.waypoints.length) {
          e.waypoints.forEach((wp, i) => {
            g.appendChild(el("circle", { cx: wp.x, cy: wp.y, r: 6, class: "wp-handle", "data-edge": e.id, "data-wp": i }));
          });
        }
      }
    },

    renderNodes() {
      const g = this.layers.nodes;
      g.innerHTML = "";
      for (const n of S.model.nodes) {
        const st = this.effectiveState(n.id); // {id,label,fill,ring,size}
        const hl = this.highlights[n.id];
        const selected = S.selection.type === "node" && S.selection.id === n.id;
        const isConnectSrc = this._connectFrom && this._connectFrom.node === n.id;

        const grp = el("g", {
          class: "node" + (selected ? " selected" : ""),
          "data-node": n.id,
          transform: `translate(${n.x},${n.y})`,
        });
        if (hl && hl.scale && hl.scale !== 1) {
          grp.setAttribute(
            "transform",
            `translate(${n.x + n.w / 2},${n.y + n.h / 2}) scale(${hl.scale}) translate(${-n.w / 2},${-n.h / 2})`
          );
        }

        const rx = n.shape === "queue" ? 4 : 14;
        const body = el("rect", {
          x: 0, y: 0, width: n.w, height: n.h, rx, ry: rx,
          class: "node-body",
          fill: st.fill,
          stroke: isConnectSrc ? "#22d3ee" : selected ? "#fff" : st.ring,
          "stroke-width": selected || isConnectSrc ? 3 : 2,
          filter: "url(#nodeShadow)",
        });
        grp.appendChild(body);

        // clip по форме узла, чтобы акцентная полоса не вылезала за скруглённые углы
        const clipId = "nodeclip-" + n.id;
        const clip = el("clipPath", { id: clipId });
        clip.appendChild(el("rect", { x: 0, y: 0, width: n.w, height: n.h, rx, ry: rx }));
        grp.appendChild(clip);

        // акцентная полоса слева (цвет типа узла), ровно по форме узла
        grp.appendChild(el("rect", { x: 0, y: 0, width: 6, height: n.h, fill: n.color, opacity: 0.9, "clip-path": "url(#" + clipId + ")" }));

        if (n.shape === "db") {
          grp.appendChild(el("ellipse", { cx: n.w / 2, cy: 10, rx: n.w / 2 - 6, ry: 6, fill: "none", stroke: st.ring, "stroke-width": 1.5, opacity: 0.6 }));
        }

        const kind = AD.NODE_KINDS[n.kind] || AD.NODE_KINDS.service;
        const icon = el("text", { x: 18, y: n.h / 2 + 6, class: "node-icon", fill: "#e6eefc" });
        icon.textContent = kind.icon;
        grp.appendChild(icon);

        const label = el("text", { x: 40, y: n.h / 2 - 4, class: "node-label" });
        label.textContent = n.label;
        grp.appendChild(label);

        const sub = el("text", { x: 40, y: n.h / 2 + 14, class: "node-sub" });
        // в базовом состоянии показываем пользовательский подзаголовок (если задан),
        // при активной смене состояния — подпись состояния (пресет или своя)
        sub.textContent = (st.id === "ok" && n.subtitle) ? n.subtitle : st.label;
        if (st.size) sub.style.fontSize = st.size + "px"; // размер шрифта подписи из шага
        grp.appendChild(sub);

        // точки соединения (порты)
        if (n.ports && n.ports.length) {
          const connectMode = this.tool === "edge";
          for (const port of n.ports) {
            grp.appendChild(el("circle", {
              cx: port.dx, cy: port.dy, r: selected || connectMode ? 6 : 4,
              class: "port" + (selected ? " active" : "") + (connectMode ? " connectable" : ""),
              "data-port": port.id, "data-pnode": n.id,
            }));
          }
        }

        // индикатор подсветки-пульса
        if (hl && hl.ring) {
          grp.appendChild(el("rect", {
            x: -6, y: -6, width: n.w + 12, height: n.h + 12, rx: rx + 6,
            fill: "none", stroke: hl.ring, "stroke-width": 3,
            opacity: hl.opacity != null ? hl.opacity : 0.9, filter: "url(#glow)",
          }));
        }
        g.appendChild(grp);
      }
    },

    /* --- события мыши --- */
    bindEvents() {
      const svg = this.svg;

      svg.addEventListener("mousedown", (ev) => {
        if (AD.engine && AD.engine.playing) return; // блок редактирования при проигрывании
        const portEl = ev.target.closest("[data-port]");
        const wpEl = ev.target.closest("[data-wp]");
        const nodeEl = ev.target.closest("[data-node]");
        const edgeEl = ev.target.closest("[data-edge]");
        const w = this.toWorld(ev.clientX, ev.clientY);

        // средняя кнопка / пробел -> pan
        if (ev.button === 1 || (ev.button === 0 && AD._space)) {
          this._pan = { x: ev.clientX, y: ev.clientY, panX: S.model.view.panX, panY: S.model.view.panY };
          ev.preventDefault();
          return;
        }
        if (ev.button !== 0) return;

        if (this.tool === "node") {
          const n = S.addNode(w.x, w.y, this.pendingKind);
          S.select("node", n.id);
          this.render();
          return;
        }

        if (this.tool === "edge") {
          // источник/цель может быть портом или узлом
          const hitNode = portEl ? portEl.dataset.pnode : nodeEl ? nodeEl.dataset.node : null;
          const hitPort = portEl ? portEl.dataset.port : null;
          if (hitNode) {
            if (!this._connectFrom) {
              this._connectFrom = { node: hitNode, port: hitPort };
            } else if (this._connectFrom.node !== hitNode) {
              const e = S.addEdge(this._connectFrom.node, hitNode, this._connectFrom.port, hitPort);
              this._connectFrom = null;
              if (e) S.select("edge", e.id);
            } else {
              this._connectFrom = null;
            }
          } else {
            this._connectFrom = null;
          }
          this.render();
          return;
        }

        // tool = select
        // Ручная детекция двойного клика: render() на первом клике пересобирает SVG,
        // из-за чего нативный dblclick не долетает до узла/связи. Определяем по времени
        // и логическому id цели — не зависит от пересборки DOM.
        const now = performance.now();
        const dkey = wpEl ? "wp:" + wpEl.dataset.edge + ":" + wpEl.dataset.wp
          : portEl ? "port:" + portEl.dataset.port
          : nodeEl ? "node:" + nodeEl.dataset.node
          : edgeEl ? "edge:" + edgeEl.dataset.edge
          : "empty";
        const isDouble = this._lastDown && now - this._lastDown.t < 350 &&
          this._lastDown.key === dkey && dkey !== "empty";
        this._lastDown = { t: now, key: dkey };
        if (isDouble) {
          this._lastDown = null;
          if (wpEl) { // двойной клик по точке изгиба — удалить
            S.removeWaypoint(wpEl.dataset.edge, parseInt(wpEl.dataset.wp, 10));
            this.render();
            return;
          }
          if (edgeEl && !nodeEl && !portEl) { // двойной клик по связи — новая точка изгиба
            const eid = edgeEl.dataset.edge;
            S.addWaypoint(eid, w.x, w.y, this.nearestSegmentIndex(eid, w));
            S.select("edge", eid);
            this.render();
            return;
          }
          if (nodeEl && !portEl) { // двойной клик по узлу — переименование
            const n = S.node(nodeEl.dataset.node);
            const name = prompt("Название узла:", n.label);
            if (name != null) { n.label = name; S.touch(); this.render(); }
            return;
          }
        }

        if (portEl) {
          // перетаскивание порта
          S.select("node", portEl.dataset.pnode);
          this._portDrag = { nodeId: portEl.dataset.pnode, portId: portEl.dataset.port, moved: false };
          this.render();
        } else if (wpEl) {
          // перетаскивание точки изгиба
          S.select("edge", wpEl.dataset.edge);
          this._wpDrag = { edgeId: wpEl.dataset.edge, index: parseInt(wpEl.dataset.wp, 10), moved: false };
          this.render();
        } else if (nodeEl) {
          const n = S.node(nodeEl.dataset.node);
          S.select("node", n.id);
          this._drag = { id: n.id, dx: w.x - n.x, dy: w.y - n.y, moved: false };
          this.render();
        } else if (edgeEl) {
          S.select("edge", edgeEl.dataset.edge);
          this.render();
        } else {
          S.clearSelection();
          this._pan = { x: ev.clientX, y: ev.clientY, panX: S.model.view.panX, panY: S.model.view.panY };
          this.render();
        }
      });

      window.addEventListener("mousemove", (ev) => {
        if (this._portDrag) {
          const w = this.toWorld(ev.clientX, ev.clientY);
          const n = S.node(this._portDrag.nodeId);
          const port = S.port(this._portDrag.nodeId, this._portDrag.portId);
          if (n && port) {
            port.dx = Math.round(w.x - n.x);
            port.dy = Math.round(w.y - n.y);
            this._portDrag.moved = true;
            this.render();
          }
        } else if (this._wpDrag) {
          const w = this.toWorld(ev.clientX, ev.clientY);
          const e = S.edge(this._wpDrag.edgeId);
          if (e && e.waypoints && e.waypoints[this._wpDrag.index]) {
            e.waypoints[this._wpDrag.index] = { x: Math.round(w.x), y: Math.round(w.y) };
            this._wpDrag.moved = true;
            this.render();
          }
        } else if (this._drag) {
          const w = this.toWorld(ev.clientX, ev.clientY);
          const n = S.node(this._drag.id);
          if (!n) return;
          n.x = Math.round(w.x - this._drag.dx);
          n.y = Math.round(w.y - this._drag.dy);
          this._drag.moved = true;
          this.render();
        } else if (this._pan) {
          S.model.view.panX = this._pan.panX + (ev.clientX - this._pan.x);
          S.model.view.panY = this._pan.panY + (ev.clientY - this._pan.y);
          this.applyView();
        }
      });

      window.addEventListener("mouseup", () => {
        if ((this._drag && this._drag.moved) ||
            (this._portDrag && this._portDrag.moved) ||
            (this._wpDrag && this._wpDrag.moved)) S.touch();
        if (this._pan) S.save();
        this._drag = null;
        this._portDrag = null;
        this._wpDrag = null;
        this._pan = null;
      });

      // нативный dblclick не используем — он ломается из-за пересборки SVG на первом
      // клике; двойной клик определяется вручную в обработчике mousedown выше.

      svg.addEventListener("wheel", (ev) => {
        ev.preventDefault();
        const factor = ev.deltaY < 0 ? 1.1 : 1 / 1.1;
        this.zoomBy(factor, ev.clientX, ev.clientY);
      }, { passive: false });
    },
  });
})();
