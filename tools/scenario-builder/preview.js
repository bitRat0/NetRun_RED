(function (root) {
  "use strict";
  const D =
    typeof module === "object" && module.exports
      ? require("./firmware-data.js")
      : root.NRData;
  const RED = 63488,
    MAGENTA = 63519,
    WHITE = 65535,
    YELLOW = 65504;
  const timing = {
    revealFrames: 6,
    revealMs: 70,
    revealHoldMs: 450,
    attackFrames: 8,
    attackMs: 45,
  };
  const rgb = (n) =>
    `rgb(${Math.round((((n >> 11) & 31) * 255) / 31)},${Math.round((((n >> 5) & 63) * 255) / 63)},${Math.round(((n & 31) * 255) / 31)})`;
  function command(ctx, c) {
    const [op, ...a] = c;
    const color = a.pop();
    ctx.fillStyle = ctx.strokeStyle = rgb(color);
    ctx.lineWidth = 1;
    const [x, y, w, h, r] = a;
    ctx.beginPath();
    if (op.endsWith("Pixel")) {
      ctx.fillRect(x, y, 1, 1);
      return;
    }
    if (op.endsWith("FastHLine")) {
      ctx.fillRect(x, y, w, 1);
      return;
    }
    if (op.endsWith("FastVLine")) {
      ctx.fillRect(x, y, 1, w);
      return;
    }
    if (op.endsWith("Line")) {
      ctx.moveTo(x + 0.5, y + 0.5);
      ctx.lineTo(w + 0.5, h + 0.5);
      ctx.stroke();
      return;
    }
    if (op.endsWith("Triangle")) {
      ctx.moveTo(x, y);
      ctx.lineTo(w, h);
      ctx.lineTo(r, a[5]);
      ctx.closePath();
    } else if (op.endsWith("Circle")) ctx.arc(x, y, w, 0, Math.PI * 2);
    else if (op.endsWith("Ellipse")) ctx.ellipse(x, y, w, h, 0, 0, Math.PI * 2);
    else if (op.endsWith("RoundRect")) ctx.roundRect(x, y, w, h, r);
    else if (op.endsWith("Rect")) {
      if (op.startsWith("fill")) ctx.fillRect(x, y, w, h);
      else ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
      return;
    } else throw Error("Missing Canvas primitive: " + op);
    if (op.startsWith("fill")) ctx.fill();
    else ctx.stroke();
  }
  function sprite(ctx, id, x = 0, y = 0) {
    const data = Object.prototype.hasOwnProperty.call(D.sprites, id)
      ? D.sprites[id]
      : null;
    if (!data) return false;
    ctx.save();
    ctx.translate(x, y);
    data.commands.forEach((c) => command(ctx, c));
    ctx.restore();
    return true;
  }
  function frameState(mode, elapsed, animation) {
    if (mode === "reveal") {
      const frame = Math.min(5, Math.floor(elapsed / 70));
      return {
        frame,
        clip: frame === 5 ? 226 : 18 + frame * 34,
        impulse: 0,
        done: elapsed >= 870,
      };
    }
    if (mode !== "attack") return { frame: 0, impulse: 0, done: true };
    const frame = Math.min(7, Math.floor(elapsed / 45));
    const impulse =
      animation === "lunge"
        ? frame < 3
          ? frame * 5
          : frame < 5
            ? 15 - frame * 3
            : 0
        : animation === "slash"
          ? frame === 2 || frame === 3
            ? 3
            : 0
          : frame === 2 || frame === 3
            ? 2
            : 0;
    return { frame, impulse, done: elapsed >= 360 };
  }
  function attackCommands(style, f, demon = false, program = null) {
    const commands = [],
      add = (op, ...a) => commands.push([op, ...a]);
    const sx = demon ? 150 : 88,
      sy = demon ? 52 : 58;
    if (program !== null) {
      const ex = 90 + f * 15,
        ey = 58;
      switch (program) {
        case "Hellbolt":
          for (let b = 0; b < 3; b++) {
            const x = ex + b * 7,
              y = ey + ((f + b) & 1 ? -5 : 5),
              h = 9 + ((f + b) % 3) * 3;
            add("fillCircle", x, y + 3, 5, RED);
            add(
              "fillTriangle",
              x - 5,
              y + 5,
              x + 5,
              y + 5,
              x + ((f + b) & 1 ? 2 : -2),
              y - h,
              RED,
            );
            add(
              "fillTriangle",
              x - 2,
              y + 3,
              x + 3,
              y + 3,
              x + 1,
              y - Math.trunc(h / 2),
              YELLOW,
            );
            add("drawPixel", x, y - h - 2, WHITE);
          }
          add("drawPixel", ex - 7, ey - 8, YELLOW);
          add("drawPixel", ex + 25, ey + 12, RED);
          break;
        case "Vrizzbolt":
          for (let s = 0; s < 3; s++) {
            const x = ex + s * 12,
              y = ey + ((s + f) & 1 ? -9 : 9);
            add("drawLine", x, y, x + 6, y + (s & 1 ? 10 : -10), 2047);
            add("drawLine", x + 6, y + (s & 1 ? 10 : -10), x + 12, y, MAGENTA);
          }
          break;
        case "Nervescrub":
          for (let b = 0; b < 3; b++)
            add(
              "drawRect",
              ex + 2 + b * 4,
              ey + (b - 1) * 11 - 4,
              22,
              9,
              b === 1 ? WHITE : MAGENTA,
            );
          add("drawFastHLine", ex + 26, ey - 13, 12, MAGENTA);
          add("drawFastHLine", ex + 29, ey + 10, 9, WHITE);
          break;
        case "Superglue": {
          const x = f >= 6 ? 185 : ex,
            o = f >= 4 ? 6 : 8,
            r = f >= 6 ? 5 : 4;
          for (const row of [-1, 1])
            for (const col of [-1, 1]) {
              add(
                "fillRoundRect",
                x + col * o - r,
                ey + row * o - r,
                r * 2 + 1,
                r * 2 + 1,
                3,
                YELLOW,
              );
              add("drawPixel", x + col * o, ey + row * o, WHITE);
            }
          if (f >= 6) add("drawRect", x - 14, ey - 14, 29, 29, YELLOW);
          break;
        }
        case "Poison Flatline":
          for (let s = 0; s < 4; s++) {
            const x = ex + s * 8;
            add("fillTriangle", x, ey, x + 8, ey - 5, x + 5, ey + 6, 2016);
          }
          add("drawFastHLine", ex + 27, ey, 13, 2016);
          add("drawPixel", ex + 34, ey - 8, WHITE);
          break;
        case "DeckKRASH":
          for (let b = 0; b < 5; b++) {
            const x = ex + (b % 3) * 11,
              y = ey + Math.trunc(b / 3) * 12 - 8;
            if ((f + b) % 2 === 0) add("fillRect", x, y, 7, 7, MAGENTA);
            else add("drawRect", x + 3, y - 3, 8, 8, RED);
          }
          add("drawFastHLine", ex + 23, ey + 13, 15, WHITE);
          break;
        default:
          add("drawCircle", ex + 12, ey, 4 + f * 2, MAGENTA);
          if (f >= 2) add("drawCircle", ex + 12, ey, 2, RED);
      }
    } else if (style === "lunge") {
      add("drawFastHLine", sx, sy, 18 + f * 8, RED);
      add("drawFastHLine", sx + 5, sy - 5, 10 + f * 5, MAGENTA);
      add("drawFastHLine", sx + 10, sy + 5, 8 + f * 4, WHITE);
    } else if (style === "burst") {
      for (let s = 0; s < 3; s++)
        if (f >= s) {
          const head = sx + (f - s) * 14,
            y = sy - 10 + s * 10;
          add("drawFastHLine", head - 17, y, 13, s === 1 ? WHITE : MAGENTA);
          add("drawPixel", head, y, RED);
        }
    } else if (style === "slash") {
      add("drawLine", sx, sy - 15, sx + 12 + f * 8, sy + 11, WHITE);
      add("drawLine", sx, sy + 11, sx + 12 + f * 8, sy - 15, MAGENTA);
    } else if (style === "pulse") {
      add("drawCircle", sx + f * 8, sy, 4 + f * 3, MAGENTA);
      if (f >= 2) add("drawCircle", sx + f * 8, sy, 1 + f * 3, RED);
    }
    if (program !== null ? f >= 6 : f >= 4 && f <= 5) {
      add("drawCircle", 185, 62, f === 4 ? 5 : 10, RED);
      add("drawFastHLine", 170, 62, 31, WHITE);
      add("drawFastVLine", 185, 47, 31, RED);
      add("drawLine", 173, 50, 197, 74, RED);
      add("drawLine", 197, 50, 173, 74, RED);
    }
    return commands;
  }
  function draw(
    canvas,
    visual,
    animation,
    mode = "idle",
    elapsed = 0,
    program = "Hellbolt",
  ) {
    if (typeof visual !== "string") visual = "unavailable";
    const ctx = canvas.getContext("2d"),
      state = frameState(mode, elapsed, animation);
    ctx.save();
    ctx.setTransform(canvas.width / 240, 0, 0, canvas.height / 135, 0, 0);
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, 240, 135);
    ctx.strokeStyle = "#38323e";
    ctx.strokeRect(7.5, 23.5, 225, 90);
    ctx.save();
    if (mode === "reveal") {
      ctx.beginPath();
      ctx.rect(8, 24, state.clip, 89);
      ctx.clip();
    }
    const demon = visual.startsWith("demon"),
      enemy = visual === "enemy";
    const offset = mode === "attack" ? state.impulse : 0;
    const available = enemy
      ? sprite(ctx, visual)
      : demon
        ? sprite(ctx, visual, 130 + offset, 28)
        : sprite(ctx, visual, 37 + offset, 32);
    ctx.restore();
    if (!available) {
      ctx.fillStyle = "#efc95f";
      ctx.font = "10px monospace";
      ctx.fillText("Preview unavailable", 20, 66);
    } else if (mode === "attack")
      attackCommands(
        animation,
        state.frame,
        demon,
        enemy ? program : null,
      ).forEach((c) => command(ctx, c));
    if (mode === "reveal" && elapsed < 420 && state.frame % 2 === 0)
      command(ctx, ["drawFastHLine", 12, 106, 200 - state.frame * 12, RED]);
    ctx.restore();
    return state;
  }
  function thumbnail(canvas, visual) {
    if (typeof visual !== "string") visual = "unavailable";
    const ctx = canvas.getContext("2d");
    ctx.save();
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.scale(canvas.width / 56, canvas.height / 56);
    sprite(
      ctx,
      visual,
      visual === "enemy" ? -36 : visual.startsWith("demon") ? 28 : 4,
      visual === "enemy" ? -24 : visual.startsWith("demon") ? 12 : 4,
    );
    ctx.restore();
  }
  function attach(canvas, visual, animation, program) {
    let mode = "idle",
      last = "reveal",
      start = 0,
      raf = 0,
      disposed = false;
    function tick(now) {
      if (disposed) return;
      const state = draw(canvas, visual, animation, mode, now - start, program);
      if (!state.done) raf = requestAnimationFrame(tick);
      else if (mode !== "idle")
        raf = requestAnimationFrame(() => {
          if (!disposed) draw(canvas, visual, animation, "idle", 0, program);
        });
    }
    function play(next) {
      cancelAnimationFrame(raf);
      mode = next === "replay" ? last : next;
      if (mode !== "idle") last = mode;
      start = performance.now();
      tick(start);
    }
    play("idle");
    return {
      play,
      dispose() {
        disposed = true;
        cancelAnimationFrame(raf);
      },
    };
  }
  const api = {
    timing,
    rgb,
    command,
    sprite,
    frameState,
    attackCommands,
    draw,
    thumbnail,
    attach,
  };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.NRPreview = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
