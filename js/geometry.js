/* =============================================================================
 * geometry.js — вся геометрия связей в одном месте (AD.geom).
 *   Построение SVG-путей делается готовой библиотекой d3 (d3-shape/d3-path),
 *   а специфические вычисления, которых нет в библиотеках (пересечение луча с
 *   прямоугольником узла, перпендикулярное смещение, укорочение под стрелку,
 *   расстояние до отрезка) вынесены сюда отдельными понятными функциями.
 * =========================================================================== */
(function () {
  "use strict";
  const AD = window.AD;
  const d3 = window.d3;

  // генератор гладкой кривой через точки (Catmull-Rom, интерполирующий) — d3-shape
  const catmull = d3.line().x((p) => p.x).y((p) => p.y).curve(d3.curveCatmullRom.alpha(0.5));

  AD.geom = {
    /** центр прямоугольного узла */
    nodeCenter(n) {
      return { x: n.x + n.w / 2, y: n.y + n.h / 2 };
    },
    /** абсолютная позиция порта узла (dx/dy — смещение от левого-верхнего угла) */
    portPos(n, port) {
      return { x: n.x + port.dx, y: n.y + port.dy };
    },

    /** точка пересечения луча из центра (cx,cy) прямоугольника (полуразмеры hw,hh)
        в направлении (tx,ty) с границей этого прямоугольника */
    borderPoint(cx, cy, hw, hh, tx, ty) {
      const dx = tx - cx, dy = ty - cy;
      if (dx === 0 && dy === 0) return { x: cx, y: cy };
      const sx = dx !== 0 ? hw / Math.abs(dx) : Infinity;
      const sy = dy !== 0 ? hh / Math.abs(dy) : Infinity;
      const s = Math.min(sx, sy);
      return { x: cx + dx * s, y: cy + dy * s };
    },

    /** контрольная точка кривой между узлами a,b: середина + смещение `curve`
        по перпендикуляру к линии центров (curve в долях длины, знак = сторона) */
    perpControl(a, b, curve) {
      const ca = this.nodeCenter(a), cb = this.nodeCenter(b);
      const mx = (ca.x + cb.x) / 2, my = (ca.y + cb.y) / 2;
      if (!curve) return { x: mx, y: my };
      const dx = cb.x - ca.x, dy = cb.y - ca.y;
      const len = Math.hypot(dx, dy) || 1;
      return { x: mx + (-dy / len) * curve * len, y: my + (dx / len) * curve * len };
    },

    /** сдвинуть точку `tip` к точке `from` на расстояние `dist`
        (конец линии стыкуется с основанием стрелки, а кончик — в tip) */
    pullBack(from, tip, dist) {
      const dx = tip.x - from.x, dy = tip.y - from.y;
      const len = Math.hypot(dx, dy) || 1;
      const d = Math.min(dist, len - 0.5);
      return { x: tip.x - (dx / len) * d, y: tip.y - (dy / len) * d };
    },

    /** расстояние от точки p до отрезка a-b (для выбора сегмента под точку изгиба) */
    distToSegment(p, a, b) {
      const dx = b.x - a.x, dy = b.y - a.y;
      const l2 = dx * dx + dy * dy;
      let t = l2 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
      t = Math.max(0, Math.min(1, t));
      return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
    },

    /** точка на пути (доля pos∈[0,1]) со смещением off по перпендикуляру
        («вверх» на экране = положительный off). Используется для подписей связи/бейджей. */
    pointAlongPath(pathEl, pos, off) {
      const L = pathEl.getTotalLength() || 1;
      pos = Math.max(0, Math.min(1, pos));
      const at = pathEl.getPointAtLength(L * pos);
      if (!off) return { x: at.x, y: at.y };
      const a = pathEl.getPointAtLength(Math.max(0, L * pos - 3));
      const b = pathEl.getPointAtLength(Math.min(L, L * pos + 3));
      const ang = Math.atan2(b.y - a.y, b.x - a.x);
      let nx = Math.cos(ang + Math.PI / 2), ny = Math.sin(ang + Math.PI / 2);
      if (ny > 0) { nx = -nx; ny = -ny; } // «вверх» = положительное направление off
      return { x: at.x + nx * off, y: at.y + ny * off };
    },

    /* --- построение SVG-путей библиотекой d3 (без ручной сборки строк) --- */

    /** гладкая кривая через список точек */
    smoothPathD(points) {
      return catmull(points);
    },
    /** прямая линия start→end */
    linePathD(start, end) {
      const p = d3.path();
      p.moveTo(start.x, start.y);
      p.lineTo(end.x, end.y);
      return p.toString();
    },
    /** квадратичная кривая start→end с контрольной точкой ctrl */
    quadPathD(start, ctrl, end) {
      const p = d3.path();
      p.moveTo(start.x, start.y);
      p.quadraticCurveTo(ctrl.x, ctrl.y, end.x, end.y);
      return p.toString();
    },
  };
})();
