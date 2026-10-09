// Animated Diagrams HTML player: draws the display list built by the WebAssembly engine
// (player/Player.cpp, ad_engine) on a <canvas> and provides playback controls.
// The decoder below mirrors src/Engine/FrameBuffer.hpp and the drawing mirrors
// UI/QtRender.cpp (RenderFrame) -- keep them in sync.
(function () {
  'use strict';

  var root = document.getElementById('ad-player');
  var canvas = root.querySelector('.ad-canvas');
  var ctx = canvas.getContext('2d');
  var message = root.querySelector('.ad-message');

  var FONT_STACK = 'system-ui, -apple-system, "Segoe UI", Roboto, "Noto Sans", "Helvetica Neue", Arial, sans-serif';
  var GENERIC_FAMILIES = { 'serif': 1, 'sans-serif': 1, 'monospace': 1, 'cursive': 1, 'fantasy': 1, 'system-ui': 1 };
  var REF_PX = 100; // text is measured and drawn with a 100 px font scaled to the real size

  // ---------------------------------------------------------------------------------------
  // Options: defaults < options embedded by the application < data-* attributes < URL query
  // ---------------------------------------------------------------------------------------
  function readJson(id) {
    var el = document.getElementById(id);
    if (!el) { return null; }
    try { return JSON.parse(el.textContent); } catch (e) { return null; }
  }

  function parseBool(v, fallback) {
    if (v === undefined || v === null) { return fallback; }
    if (typeof v === 'boolean') { return v; }
    v = String(v).toLowerCase();
    if (v === '' || v === '1' || v === 'true' || v === 'yes' || v === 'on') { return true; }
    if (v === '0' || v === 'false' || v === 'no' || v === 'off') { return false; }
    return fallback;
  }

  var framed = (function () { try { return window.self !== window.top; } catch (e) { return true; } })();
  var opts = { autoplay: true, loop: true, controls: 'auto', segment: 0, t: 0, speed: 1, keys: framed ? 'focus' : '1', src: '' };
  var embedded = readJson('ad-player-options');
  if (embedded && typeof embedded === 'object') {
    Object.keys(opts).forEach(function (k) { if (k in embedded) { opts[k] = embedded[k]; } });
  }
  var explicitAutoplay = false;
  function applyOption(k, v) {
    if (!(k in opts)) { return; }
    if (k === 'autoplay') { explicitAutoplay = true; }
    opts[k] = v;
  }
  Object.keys(root.dataset).forEach(function (k) { applyOption(k, root.dataset[k]); });
  new URLSearchParams(location.search).forEach(function (v, k) { applyOption(k, v); });
  opts.autoplay = parseBool(opts.autoplay, true);
  opts.loop = parseBool(opts.loop, true);
  opts.controls = String(opts.controls) === 'auto' ? 'auto' : (parseBool(opts.controls, true) ? 'on' : 'off');
  opts.keys = String(opts.keys) === 'focus' ? 'focus' : (parseBool(opts.keys, true) ? 'on' : 'off');
  opts.speed = Math.min(8, Math.max(0.1, Number(opts.speed) || 1));
  // respect "reduce motion" unless autoplay was asked for explicitly
  if (!explicitAutoplay && window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches) {
    opts.autoplay = false;
  }

  function showMessage(text) {
    message.textContent = text;
    message.hidden = !text;
  }

  // ---------------------------------------------------------------------------------------
  // Text: measured by the browser, the engine asks through Module.adMeasureText
  // ---------------------------------------------------------------------------------------
  var measureCtx = document.createElement('canvas').getContext('2d');
  var fontCache = {};
  var widthCache = new Map();
  var xHeightCache = {};

  function fontCss(bold, family) {
    var key = (bold ? 'b' : 'r') + family;
    var css = fontCache[key];
    if (!css) {
      var fam = FONT_STACK;
      if (family) {
        fam = (GENERIC_FAMILIES[family] ? family : '"' + family.replace(/["\\]/g, '') + '"') + ', ' + FONT_STACK;
      }
      css = fontCache[key] = (bold ? 'bold ' : '') + REF_PX + 'px ' + fam;
    }
    return css;
  }

  function measureText(text, bold, family) {
    var css = fontCss(bold, family);
    var key = css + '\u001f' + text;
    var w = widthCache.get(key);
    if (w === undefined) {
      measureCtx.font = css;
      w = measureCtx.measureText(text).width;
      widthCache.set(key, w);
    }
    return w;
  }

  function xHeight(css) {
    var h = xHeightCache[css];
    if (h === undefined) {
      measureCtx.font = css;
      var m = measureCtx.measureText('x');
      h = xHeightCache[css] = m.actualBoundingBoxAscent || REF_PX * 0.52;
    }
    return h;
  }

  // ---------------------------------------------------------------------------------------
  // Drawing the display list (FrameBuffer layout)
  // ---------------------------------------------------------------------------------------
  var colorCache = new Map();
  function rgba(rgb, alpha) {
    var key = rgb + alpha * 0x1000000;
    var s = colorCache.get(key);
    if (!s) {
      var r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
      s = alpha >= 1 ? 'rgb(' + r + ',' + g + ',' + b + ')' : 'rgba(' + r + ',' + g + ',' + b + ',' + alpha + ')';
      colorCache.set(key, s);
    }
    return s;
  }

  var data = null, pos = 0, strings = null, decoder = new TextDecoder();
  var deviceScale = 1; // world -> device pixels (for cosmetic 0-width lines)

  function readPath() {
    var n = data[pos++];
    var path = new Path2D();
    for (var i = 0; i < n; i++) {
      switch (data[pos++]) {
        case 0: path.moveTo(data[pos], data[pos + 1]); pos += 2; break;
        case 1: path.lineTo(data[pos], data[pos + 1]); pos += 2; break;
        case 2: path.quadraticCurveTo(data[pos], data[pos + 1], data[pos + 2], data[pos + 3]); pos += 4; break;
        case 3: path.bezierCurveTo(data[pos], data[pos + 1], data[pos + 2], data[pos + 3], data[pos + 4], data[pos + 5]); pos += 6; break;
        default: path.closePath(); break;
      }
    }
    return path;
  }

  function readStroke() {
    var s = { color: data[pos++], width: data[pos++], round: data[pos++] !== 0, offset: data[pos++], dash: [] };
    var n = data[pos++];
    for (var i = 0; i < n; i++) { s.dash.push(data[pos++]); }
    return s;
  }

  function readString() {
    var off = data[pos++], len = data[pos++];
    return len > 0 ? decoder.decode(strings.subarray(off, off + len)) : '';
  }

  // Qt pen semantics: width 0 is a 1 device pixel line, dashes are given in pen widths of
  // the base stroke (so wider glow passes stretch them), odd patterns repeat.
  function setPen(s, alpha, extra) {
    var base = s.width;
    var w = base + extra;
    ctx.strokeStyle = rgba(s.color, alpha);
    ctx.lineWidth = w > 0 ? w : 1 / deviceScale;
    ctx.lineCap = s.round ? 'round' : 'butt';
    ctx.lineJoin = 'round';
    if (s.dash.length && base > 0) {
      var k = w / base;
      ctx.setLineDash(s.dash.map(function (d) { return Math.max(0.01 * base, d) * k; }));
      ctx.lineDashOffset = s.offset * k;
    } else {
      ctx.setLineDash([]);
    }
  }

  function strokeSoft(path, color, width) {
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.setLineDash([]);
    ctx.stroke(path);
  }

  function paintPath(path, effect, fill, stroke) {
    if (effect === 1) { // shadow: translucent passes shifted down
      ctx.save();
      ctx.translate(0, 3);
      strokeSoft(path, 'rgba(0,0,0,0.063)', 7);
      strokeSoft(path, 'rgba(0,0,0,0.063)', 4);
      strokeSoft(path, 'rgba(0,0,0,0.063)', 1.5);
      if (fill >= 0) {
        ctx.fillStyle = 'rgba(0,0,0,0.157)';
        ctx.fill(path, 'evenodd');
      }
      ctx.restore();
    } else if (effect === 2) { // glow: wide translucent strokes under the shape
      if (stroke) {
        setPen(stroke, 0.12, 8);
        ctx.stroke(path);
        setPen(stroke, 0.22, 4);
        ctx.stroke(path);
      } else if (fill >= 0) {
        strokeSoft(path, rgba(fill, 0.18), 8);
        strokeSoft(path, rgba(fill, 0.18), 4);
      }
    }
    if (fill >= 0) {
      ctx.fillStyle = rgba(fill, 1);
      ctx.fill(path, 'evenodd');
    }
    if (stroke) {
      setPen(stroke, 1, 0);
      ctx.stroke(path);
    }
  }

  function roundedRect(x, y, w, h, r) {
    var p = new Path2D();
    r = Math.max(0, Math.min(r, w / 2, h / 2));
    if (r <= 0) {
      p.rect(x, y, w, h);
      return p;
    }
    p.moveTo(x + r, y);
    p.lineTo(x + w - r, y);
    p.arcTo(x + w, y, x + w, y + r, r);
    p.lineTo(x + w, y + h - r);
    p.arcTo(x + w, y + h, x + w - r, y + h, r);
    p.lineTo(x + r, y + h);
    p.arcTo(x, y + h, x, y + h - r, r);
    p.lineTo(x, y + r);
    p.arcTo(x, y, x + r, y, r);
    p.closePath();
    return p;
  }

  function drawText(fill) {
    var x = data[pos++], y = data[pos++];
    var text = readString();
    var size = data[pos++], bold = data[pos++] !== 0;
    var family = readString();
    var align = data[pos++], valign = data[pos++];
    var halo = null;
    if (data[pos++] !== 0) {
      halo = { color: data[pos++], width: data[pos++] };
    }
    if (!text || size <= 0) { return; }
    var css = fontCss(bold, family);
    var k = size / REF_PX;
    var w = measureText(text, bold, family) * k;
    if (align === 1) { x -= w / 2; } else if (align === 2) { x -= w; }
    if (valign === 1) { y += xHeight(css) * k / 2; } // as dominant-baseline: middle
    ctx.translate(x, y);
    ctx.scale(k, k);
    ctx.font = css;
    if (halo) {
      ctx.strokeStyle = rgba(halo.color, 1);
      ctx.lineWidth = halo.width / k;
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      ctx.setLineDash([]);
      ctx.strokeText(text, 0, 0);
    }
    ctx.fillStyle = rgba(fill >= 0 ? fill : 0xffffff, 1);
    ctx.fillText(text, 0, 0);
  }

  function drawItems() {
    var count = data[1];
    pos = 2;
    ctx.textAlign = 'left';
    ctx.textBaseline = 'alphabetic';
    for (var i = 0; i < count; i++) {
      var tag = data[pos++], opacity = data[pos++], effect = data[pos++], flags = data[pos++];
      var fill = (flags & 1) ? data[pos++] : -1;
      var stroke = (flags & 2) ? readStroke() : null;
      ctx.save();
      if (flags & 4) {
        var ox = data[pos], oy = data[pos + 1], rot = data[pos + 2], sc = data[pos + 3], tx = data[pos + 4], ty = data[pos + 5];
        pos += 6;
        ctx.translate(ox + tx, oy + ty);
        ctx.rotate(rot * Math.PI / 180);
        ctx.scale(sc, sc);
        ctx.translate(-ox, -oy);
      }
      if (flags & 8) {
        ctx.clip(readPath());
      }
      ctx.globalAlpha = Math.min(1, Math.max(0, opacity));
      var p;
      switch (tag) {
        case 1: // rect
          p = roundedRect(data[pos], data[pos + 1], data[pos + 2], data[pos + 3], data[pos + 4]);
          pos += 5;
          paintPath(p, effect, fill, stroke);
          break;
        case 2: // ellipse
          p = new Path2D();
          p.ellipse(data[pos], data[pos + 1], Math.abs(data[pos + 2]), Math.abs(data[pos + 3]), 0, 0, 2 * Math.PI);
          pos += 4;
          paintPath(p, effect, fill, stroke);
          break;
        case 3: // path
          paintPath(readPath(), effect, fill, stroke);
          break;
        case 4: // arc: degrees, clockwise on screen, stroke only
          var a0 = data[pos + 3] * Math.PI / 180, sweep = data[pos + 4] * Math.PI / 180;
          p = new Path2D();
          p.arc(data[pos], data[pos + 1], Math.abs(data[pos + 2]), a0, a0 + sweep, sweep < 0);
          pos += 5;
          paintPath(p, effect, -1, stroke);
          break;
        case 5:
          drawText(fill);
          break;
        default:
          ctx.restore();
          throw new Error('unknown display list item ' + tag);
      }
      ctx.restore();
    }
  }

  // ---------------------------------------------------------------------------------------
  // Player
  // ---------------------------------------------------------------------------------------
  var M = null;           // the WebAssembly module
  var duration = 0;       // ms
  var bounds = null;      // world rectangle to show
  var background = '#0a111f';
  var bounds_dirty = false;
  var t = 0;
  var playing = false;
  var stopAt = -1;        // playing one segment: stop at this time
  var lastTs = 0;
  var rafId = 0;
  var boundaries = [];    // segment boundaries: 0, markers..., duration
  var markers = [];       // [{t, label}]
  var ui = null;

  function readMarkers(doc) {
    var list = doc && doc.scenario && Array.isArray(doc.scenario.markers) ? doc.scenario.markers : [];
    var out = [];
    list.forEach(function (m) {
      var time = typeof m === 'number' ? m : (m && (m.t !== undefined ? m.t : m.at !== undefined ? m.at : m.time !== undefined ? m.time : m.start));
      time = Number(time);
      if (isFinite(time) && time > 0 && time < duration) {
        out.push({ t: time, label: (m && (m.label || m.name || m.title)) || '' });
      }
    });
    out.sort(function (a, b) { return a.t - b.t; });
    return out.filter(function (m, i) { return i === 0 || m.t - out[i - 1].t > 1; });
  }

  function segmentAt(time) {
    var i = boundaries.length - 2;
    while (i > 0 && time < boundaries[i]) { i--; }
    return Math.max(0, i);
  }

  function render() {
    rafId = 0;
    if (!M) { return; }
    var dpr = window.devicePixelRatio || 1;
    var cw = Math.max(1, Math.round(canvas.clientWidth * dpr));
    var ch = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== cw || canvas.height !== ch) {
      canvas.width = cw;
      canvas.height = ch;
    }
    if (bounds_dirty) {
      bounds = [0, 1, 2, 3].map(function (i) { return M._ad_player_bounds(i); });
      bounds_dirty = false;
    }
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalAlpha = 1;
    ctx.fillStyle = background;
    ctx.fillRect(0, 0, cw, ch);
    // fit the content like the application's export framing, centered
    var k = Math.min(cw / bounds[2], ch / bounds[3]);
    deviceScale = k;
    ctx.setTransform(k, 0, 0, k, (cw - bounds[2] * k) / 2 - bounds[0] * k, (ch - bounds[3] * k) / 2 - bounds[1] * k);

    var n = M._ad_player_build(t);
    data = new Float64Array(M.HEAPF64.buffer, M._ad_player_frame(), n);
    var sp = M._ad_player_strings();
    strings = M.HEAPU8.subarray(sp, sp + M._ad_player_strings_size());
    if (n > 0) { drawItems(); }
    data = null;
    strings = null;
    updateUi();
  }

  function requestRender() {
    if (!rafId) { rafId = requestAnimationFrame(frame); }
  }

  function frame(ts) {
    rafId = 0;
    if (playing) {
      var dt = lastTs ? Math.min(250, ts - lastTs) : 0;
      lastTs = ts;
      t += dt * opts.speed;
      if (stopAt >= 0 && t >= stopAt) {
        t = stopAt;
        pause();
      } else if (t >= duration) {
        if (opts.loop && stopAt < 0) {
          t = duration > 0 ? t % duration : 0;
        } else {
          t = duration;
          pause();
        }
      }
    }
    render();
    if (playing) { requestRender(); }
  }

  function play(until) {
    if (!M) { return; }
    if (t >= duration && (until === undefined || until < 0)) { t = 0; }
    stopAt = until === undefined ? -1 : until;
    playing = true;
    lastTs = 0;
    requestRender();
  }

  function pause() {
    playing = false;
    stopAt = -1;
    requestRender();
  }

  function seek(time) {
    t = Math.min(duration, Math.max(0, time));
    lastTs = 0;
    requestRender();
  }

  function togglePlay() {
    if (playing) { pause(); } else { play(); }
  }

  /// Next segment: play the current segment to its end and stop there; while a segment is
  /// playing, jump to its end. Without markers: play / pause.
  function next() {
    if (markers.length === 0) { togglePlay(); return; }
    if (t >= duration - 0.5) {
      if (opts.loop) { seek(0); play(boundaries[1]); }
      return;
    }
    var i = segmentAt(t);
    if (playing && stopAt >= 0) {
      seek(boundaries[i + 1]);
      pause();
      return;
    }
    play(boundaries[i + 1]);
  }

  function prev() {
    if (markers.length === 0) { seek(t - 1000); return; }
    var i = segmentAt(t);
    var target = (t - boundaries[i] < 300 && i > 0) ? boundaries[i - 1] : boundaries[i];
    pause();
    seek(target);
  }

  function startPosition() {
    var s = Math.floor(Number(opts.segment) || 0);
    if (s >= 1 && boundaries.length > 1) {
      return boundaries[Math.min(s, boundaries.length - 1) - 1];
    }
    return Math.min(duration, Math.max(0, (Number(opts.t) || 0) * 1000));
  }

  function start() {
    seek(startPosition());
    if (opts.autoplay) {
      if (markers.length) { play(boundaries[segmentAt(t) + 1]); } else { play(); }
    } else {
      pause();
    }
  }

  // ---------------------------------------------------------------------------------------
  // Controls
  // ---------------------------------------------------------------------------------------
  var ICONS = {
    play: 'M5 3l11 6-11 6z',
    pause: 'M4 3h4v12H4zM10 3h4v12h-4z',
    prev: 'M3 3h2v12H3zM16 3v12L6 9z',
    next: 'M13 3h2v12h-2zM2 3l10 6-10 6z',
    loop: 'M4 6h8V3l4 4-4 4V8H5v3H3V7a1 1 0 0 1 1-1zm10 6H6v3l-4-4 4-4v3h7V7h2v4a1 1 0 0 1-1 1z'
  };

  function button(cls, icon, title, onClick) {
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'ad-btn ' + cls;
    b.title = title;
    b.setAttribute('aria-label', title);
    b.innerHTML = '<svg viewBox="0 0 18 18" aria-hidden="true"><path d="' + ICONS[icon] + '"/></svg>';
    b.addEventListener('click', function (e) { e.stopPropagation(); onClick(); });
    return b;
  }

  function setIcon(b, icon) { b.querySelector('path').setAttribute('d', ICONS[icon]); }

  function formatTime(ms) { return (ms / 1000).toFixed(1); }

  function buildUi() {
    var bar = document.createElement('div');
    bar.className = 'ad-bar';
    var prevBtn = button('ad-prev', 'prev', 'Previous segment (\u2190)', prev);
    var playBtn = button('ad-play', 'play', 'Play / pause', togglePlay);
    var nextBtn = button('ad-next', 'next', 'Next segment (\u2192 / Space)', next);
    var seekBox = document.createElement('div');
    seekBox.className = 'ad-seek';
    var range = document.createElement('input');
    range.type = 'range';
    range.min = '0';
    range.max = String(duration);
    range.step = 'any';
    range.setAttribute('aria-label', 'Position');
    range.addEventListener('input', function () { pause(); seek(Number(range.value)); });
    range.addEventListener('keydown', function (e) { e.stopPropagation(); }); // the slider has its own keys
    seekBox.appendChild(range);
    markers.forEach(function (m, i) {
      var tick = document.createElement('button');
      tick.type = 'button';
      tick.className = 'ad-tick';
      tick.style.left = 'calc(8px + (100% - 16px) * ' + (m.t / duration) + ')'; // the slider thumb travel
      tick.title = m.label || ('Segment ' + (i + 2));
      tick.setAttribute('aria-label', tick.title);
      tick.addEventListener('click', function (e) { e.stopPropagation(); pause(); seek(m.t); });
      seekBox.appendChild(tick);
    });
    var time = document.createElement('span');
    time.className = 'ad-time';
    var loopBtn = button('ad-loop', 'loop', 'Loop', function () { opts.loop = !opts.loop; updateUi(); });
    prevBtn.hidden = nextBtn.hidden = markers.length === 0;
    [prevBtn, playBtn, nextBtn, seekBox, time, loopBtn].forEach(function (el) { bar.appendChild(el); });
    bar.addEventListener('click', function (e) { e.stopPropagation(); });
    root.appendChild(bar);
    ui = { bar: bar, play: playBtn, range: range, time: time, loop: loopBtn, shownPlaying: null };
  }

  function updateUi() {
    if (!ui) { return; }
    if (ui.shownPlaying !== playing) {
      ui.shownPlaying = playing;
      setIcon(ui.play, playing ? 'pause' : 'play');
      ui.play.title = playing ? 'Pause' : 'Play';
    }
    if (document.activeElement !== ui.range) { ui.range.value = String(t); }
    var label = formatTime(t) + ' / ' + formatTime(duration) + ' s';
    if (markers.length) { label = (segmentAt(t) + 1) + '/' + (boundaries.length - 1) + ' \u00b7 ' + label; }
    if (ui.time.textContent !== label) { ui.time.textContent = label; }
    ui.loop.setAttribute('aria-pressed', opts.loop ? 'true' : 'false');
  }

  var hideTimer = 0;
  function showControls() {
    if (opts.controls !== 'auto') { return; }
    root.classList.add('ad-controls-on');
    clearTimeout(hideTimer);
    hideTimer = setTimeout(function () {
      if (!root.contains(document.activeElement) || document.activeElement === root) {
        root.classList.remove('ad-controls-on');
      }
    }, 2200);
  }

  // ---------------------------------------------------------------------------------------
  // Input: keyboard (only while focused unless keys=1), pointer, host page messages
  // ---------------------------------------------------------------------------------------
  function onKey(e) {
    if (!M || e.altKey || e.ctrlKey || e.metaKey) { return; }
    var handled = true;
    switch (e.key) {
      case ' ':
      case 'Spacebar':
      case 'ArrowRight':
      case 'PageDown':
        if (e.key === 'ArrowRight' && markers.length === 0) { seek(t + 1000); } else { next(); }
        break;
      case 'ArrowLeft':
      case 'PageUp':
        prev();
        break;
      case 'Home': pause(); seek(0); break;
      case 'End': pause(); seek(duration); break;
      case 'k': case 'K': togglePlay(); break;
      case 'l': case 'L': opts.loop = !opts.loop; updateUi(); break;
      case 'Escape': root.blur(); break;
      default: handled = false;
    }
    if (handled) {
      e.preventDefault();
      e.stopPropagation();
      showControls();
    }
  }

  function onMessage(e) {
    var d = e.data;
    if (d === 'slide:start') { start(); return; }   // reveal.js: the slide with the iframe is shown
    if (d === 'slide:stop') { pause(); return; }     // ... and hidden
    if (!d || typeof d !== 'object' || d.type !== 'ad-player') { return; }
    switch (d.action) {
      case 'play': play(); break;
      case 'pause': pause(); break;
      case 'toggle': togglePlay(); break;
      case 'next': next(); break;
      case 'prev': prev(); break;
      case 'restart': start(); break;
      case 'seek': pause(); seek(Number(d.time) || 0); break;
      default: break;
    }
  }

  function installInput() {
    if (opts.keys === 'on') {
      window.addEventListener('keydown', onKey);
    } else if (opts.keys === 'focus') {
      root.addEventListener('keydown', onKey);
    }
    canvas.addEventListener('click', function () {
      root.focus({ preventScroll: true });
      if (markers.length) { next(); } else { togglePlay(); }
      showControls();
    });
    root.addEventListener('pointermove', showControls);
    root.addEventListener('focusin', showControls);
    root.addEventListener('pointerleave', function () {
      if (opts.controls === 'auto' && !root.contains(document.activeElement)) { root.classList.remove('ad-controls-on'); }
    });
    window.addEventListener('message', onMessage);
    if (window.ResizeObserver) {
      new ResizeObserver(requestRender).observe(root);
    } else {
      window.addEventListener('resize', requestRender);
    }
    window.addEventListener('resize', requestRender); // devicePixelRatio changes (zoom)
    if (document.fonts && document.fonts.addEventListener) {
      document.fonts.addEventListener('loadingdone', fontsChanged);
    }
  }

  function fontsChanged() {
    if (!M) { return; }
    widthCache.clear();
    xHeightCache = {};
    M._ad_player_fonts_changed();
    bounds_dirty = true;
    requestRender();
  }

  // ---------------------------------------------------------------------------------------
  // Startup
  // ---------------------------------------------------------------------------------------
  function loadDocument(text) {
    var doc;
    try { doc = JSON.parse(text); } catch (e) { showMessage('Invalid document: ' + e.message); return; }
    var size = M.lengthBytesUTF8(text) + 1;
    var ptr = M._malloc(size);
    M.stringToUTF8(text, ptr, size);
    var ok = M._ad_player_load(ptr, size - 1);
    M._free(ptr);
    if (!ok) {
      showMessage('Cannot open the document: ' + M.UTF8ToString(M._ad_player_error()));
      return;
    }
    duration = M._ad_player_duration();
    background = M.UTF8ToString(M._ad_player_background()) || background;
    document.documentElement.style.background = background;
    document.body.style.background = background;
    bounds_dirty = true;
    markers = readMarkers(doc);
    boundaries = [0].concat(markers.map(function (m) { return m.t; }), [duration]);
    if (!root.hasAttribute('aria-label')) {
      root.setAttribute('aria-label', (doc.meta && doc.meta.name) || document.title || 'Animated diagram');
    }
    if (opts.controls !== 'off') { buildUi(); }
    if (opts.controls === 'on') { root.classList.add('ad-controls-on'); }
    installInput();
    showMessage('');
    start();
  }

  function documentText() {
    var el = document.getElementById('ad-document');
    var text = el ? el.textContent.trim() : '';
    if (text.charAt(0) === '{') { return Promise.resolve(text); }
    if (opts.src) { // the bare template: ?src=diagram.json
      return fetch(opts.src).then(function (r) {
        if (!r.ok) { throw new Error(r.status + ' ' + r.statusText); }
        return r.text();
      });
    }
    return Promise.reject(new Error('no document (export one from Animated Diagrams or pass ?src=diagram.json)'));
  }

  if (typeof AdPlayerEngine !== 'function') {
    showMessage('The player engine is missing.');
    return;
  }
  Promise.all([AdPlayerEngine({ adMeasureText: measureText }), documentText()]).then(function (r) {
    M = r[0];
    loadDocument(r[1]);
  }).catch(function (e) {
    showMessage('Cannot start the player: ' + (e && e.message ? e.message : e));
  });
})();
