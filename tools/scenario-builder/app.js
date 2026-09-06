(function () {
  "use strict";
  const M = NRModel,
    P = NRPreview,
    D = NRData,
    $ = (id) => document.getElementById(id);
  const names = {
    scenario: "Scenario / Architecture",
    black_ice: "Black ICE",
    demons: "Demons",
    enemies: "Enemy Netrunners",
  };
  const state = {
    kind: "scenario",
    docs: Object.fromEntries(M.kinds.map((k) => [k, M.blank(k)])),
    loaded: { black_ice: false, demons: false, enemies: false },
    selected: { scenario: 0, black_ice: 0, demons: 0, enemies: 0 },
    dirty: false,
    rawDirty: false,
    previews: [],
    summaries: [],
    details: {},
    graphPositions: {},
    graphText: "",
    graphIssues: [],
    suppressAutosave: false,
  };
  const WORKSPACE_STORAGE_KEY = "netrun-red-scenario-builder-workspace";
  const WORKSPACE_VERSION = 1;
  let saveTimer = 0;
  function workspaceSnapshot() {
    return {
      workspaceVersion: WORKSPACE_VERSION,
      kind: state.kind,
      docs: M.clone(state.docs),
      loaded: M.clone(state.loaded),
      selected: M.clone(state.selected),
      details: M.clone(state.details),
      graphPositions: M.clone(state.graphPositions),
      graphText: state.graphText,
    };
  }
  function saveWorkspaceNow() {
    saveTimer = 0;
    try {
      localStorage.setItem(WORKSPACE_STORAGE_KEY, JSON.stringify(workspaceSnapshot()));
      state.dirty = state.rawDirty;
      notice("Saved locally");
    } catch (e) {
      notice("Local save unavailable");
    }
  }
  function scheduleSave() {
    if (state.suppressAutosave) return;
    clearTimeout(saveTimer);
    notice("Saving locally...");
    saveTimer = setTimeout(saveWorkspaceNow, 180);
  }
  function validWorkspace(saved) {
    if (!saved || saved.workspaceVersion !== WORKSPACE_VERSION || !M.object(saved.docs) ||
        !M.object(saved.loaded) || !M.object(saved.selected)) return false;
    if (!M.kinds.every((k) => M.object(saved.docs[k]))) return false;
    if (!M.kinds.slice(1).every((k) => typeof saved.loaded[k] === "boolean")) return false;
    if (!M.kinds.every((k) => Number.isInteger(saved.selected[k]) && saved.selected[k] >= 0)) return false;
    return saved.kind === undefined || M.kinds.includes(saved.kind);
  }
  function restoreWorkspace() {
    let raw;
    try {
      raw = localStorage.getItem(WORKSPACE_STORAGE_KEY);
    } catch (e) {
      return "Local workspace unavailable";
    }
    if (raw === null) return "";
    try {
      const saved = JSON.parse(raw);
      if (!validWorkspace(saved)) return "Saved local workspace is unsupported or malformed";
      state.docs = saved.docs;
      state.loaded = { ...state.loaded, ...saved.loaded };
      state.selected = { ...state.selected, ...saved.selected };
      state.kind = saved.kind || "scenario";
      state.details = M.object(saved.details) ? saved.details : {};
      state.graphPositions = M.object(saved.graphPositions) ? saved.graphPositions : {};
      state.graphText = typeof saved.graphText === "string" ? saved.graphText : "";
      state.dirty = false;
      return "Restored local workspace";
    } catch (e) {
      return "Saved local workspace is malformed";
    }
  }
  const catalogs = () =>
    Object.fromEntries(
      M.kinds.slice(1).map((k) => [k, state.loaded[k] ? state.docs[k] : null]),
    );
  function el(tag, attrs = {}, ...children) {
    const n = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "class") n.className = v;
      else if (k.startsWith("on")) n.addEventListener(k.slice(2), v);
      else if (k === "text") n.textContent = v;
      else n.setAttribute(k, v);
    }
    children.flat().forEach((c) => {
      if (c !== null && c !== undefined)
        n.append(c instanceof Node ? c : document.createTextNode(M.display(c)));
    });
    return n;
  }
  const button = (text, fn, cls = "") =>
    el("button", { type: "button", class: cls, onclick: fn }, text);
  function notice(text) {
    $("notice").textContent = text;
  }
  function edited(redraw = false) {
    state.dirty = true;
    if (state.kind !== "scenario") state.loaded[state.kind] = true;
    if (redraw) render();
    else validate();
    scheduleSave();
  }
  function canEdit() {
    if (state.rawDirty) {
      notice("Apply or discard JSON text edits first.");
      return false;
    }
    return true;
  }
  function mutate(fn) {
    if (!canEdit()) return;
    fn();
    edited(true);
  }
  function remove(fn, message) {
    if (canEdit() && confirm(message)) {
      fn();
      edited(true);
    }
  }
  function uniqueId(entries, base) {
    let id = base.slice(0, 25),
      n = 1;
    while (entries.some((e) => e?.id === id))
      id = base.slice(0, 25) + "_" + n++;
    return id;
  }
  function field(parent, obj, key, options = {}) {
    const p = options.path || `$.${key}`,
      present = M.has(obj, key),
      wrap = el("label", { class: "field" });
    if (options.optional) {
      const toggle = el("input", {
        type: "checkbox",
        "aria-label": `Include ${p}`,
      });
      toggle.checked = present;
      toggle.addEventListener("change", () =>
        mutate(() => {
          if (toggle.checked)
            obj[key] = M.clone(
              options.default ??
                (options.type === "number"
                  ? 0
                  : options.values
                    ? options.values[0]
                    : ""),
            );
          else delete obj[key];
        }),
      );
      wrap.append(el("span", { class: "optional" }, toggle, "Include field"));
    }
    wrap.append(el("span", { class: "field-name" }, options.label || key));
    let input;
    if (options.values) {
      input = el("select", { "data-path": p, "aria-label": p });
      for (const value of options.values)
        input.append(el("option", { value }, options.labels?.[value] || value));
      if (present && !options.values.includes(obj[key]))
        input.prepend(
          el(
            "option",
            { value: M.display(obj[key]) },
            obj[key] === null
              ? "null / firmware default"
              : `Invalid: ${M.display(obj[key])}`,
          ),
        );
      input.value = M.display(
        obj[key] ?? (present ? "null" : options.values[0]),
      );
    } else if (options.type === "boolean") {
      input = el(
        "select",
        { "data-path": p, "aria-label": p },
        el("option", { value: "false" }, "false"),
        el("option", { value: "true" }, "true"),
      );
      if (present && typeof obj[key] !== "boolean")
        input.prepend(
          el("option", { value: M.display(obj[key]) }, M.display(obj[key])),
        );
      input.value = M.display(obj[key]);
    } else {
      input = el(options.multiline ? "textarea" : "input", {
        "data-path": p,
        "aria-label": p,
      });
      if (!options.multiline) input.type = options.type || "text";
      if (options.type === "number") {
        input.step = "1";
        if (options.min !== undefined) input.min = options.min;
        if (options.max !== undefined) input.max = options.max;
      }
      input.value = present
        ? typeof obj[key] === "object"
          ? JSON.stringify(obj[key])
          : String(obj[key])
        : "";
      if (options.list) input.setAttribute("list", options.list);
    }
    input.disabled = options.optional && !present;
    input.addEventListener("input", () => {
      if (!canEdit()) return;
      if (options.values || options.type === "boolean") return;
      obj[key] =
        options.type === "number"
          ? input.value === ""
            ? null
            : Number(input.value)
          : input.value;
      edited();
    });
    input.addEventListener("change", () => {
      if (!canEdit()) return;
      if (options.values) obj[key] = input.value;
      else if (options.type === "boolean") obj[key] = input.value === "true";
      edited(Boolean(options.values) || options.type === "boolean");
    });
    wrap.append(input);
    if (options.help) wrap.append(el("small", {}, options.help));
    parent.append(wrap);
    return input;
  }
  function num(parent, o, k, min, max, path, optional = false) {
    return field(parent, o, k, {
      type: "number",
      min,
      max,
      path: `${path}.${k}`,
      optional,
      default: min,
      help: `Integer ${min}–${max}`,
    });
  }
  function string(parent, o, k, path, max, optional = false) {
    field(parent, o, k, {
      path: `${path}.${k}`,
      optional,
      help: `≤ ${max} UTF-8 bytes`,
      multiline: k === "description",
    });
  }
  function listEditor(parent, obj, key, path, values = null, numeric = false) {
    const a = obj[key];
    parent.append(el("h3", {}, key));
    if (!Array.isArray(a)) {
      parent.append(
        el(
          "p",
          { class: "hint" },
          "Missing / invalid array. Use JSON repair or initialize explicitly.",
        ),
        button("Initialize " + key, () =>
          mutate(() => {
            obj[key] = [];
          }),
        ),
      );
      return;
    }
    a.forEach((value, i) => {
      const row = el("div", { class: "row" });
      field(row, a, i, {
        label: key,
        path: `${path}.${key}[${i}]`,
        values,
        type: numeric ? "number" : "text",
      });
      row.append(
        button(
          "Remove",
          () => remove(() => a.splice(i, 1), "Remove this " + key + " entry?"),
          "danger",
        ),
      );
      parent.append(row);
    });
    parent.append(
      button("Add " + key, () =>
        mutate(() => a.push(values ? values[0] : numeric ? 0 : "")),
      ),
    );
  }
  function referenceField(parent, o, key, kind, path, optional = true) {
    const id = "refs-" + kind;
    field(parent, o, key, {
      path: `${path}.${key}`,
      optional,
      list: id,
      help: "Stable ID: local → loaded catalog → builtin. Missing catalog = incomplete validation.",
    });
    if (M.has(o, key)) {
      const box = el("div");
      parent.append(box);
      state.summaries.push(() => {
        box.replaceChildren();
        placement(box, kind, o[key]);
      });
    }
  }
  function visualFor(kind, e) {
    if (kind === "black_ice") return e.visual;
    if (kind === "enemies") return "enemy";
    if (kind === "demons") return D.demonVisuals.find((v) => v.id === e.visual)?.sprite || D.demonVisuals[0]?.sprite || "demon1";
    return "unavailable";
  }
  function placement(parent, kind, id) {
    const found = M.resolve(kind, id, state.docs.scenario, catalogs()),
      row = el("div", { class: "placement" });
    if (found) {
      const c = el("canvas", {
        width: 56,
        height: 56,
        "aria-label": `${M.display(id)} thumbnail`,
      });
      P.thumbnail(c, visualFor(kind, found.entry));
      row.append(
        c,
        el(
          "span",
          {},
          `${M.display(id)} / ${found.scope}`,
          el("br"),
          found.entry.name || found.entry.handle || "",
        ),
      );
    } else
      row.append(
        el(
          "span",
          { class: "hint" },
          `${M.display(id)} — Preview unavailable: definition not loaded / unresolved`,
        ),
      );
    parent.append(row);
  }
  function preview(parent, kind, e) {
    const box = el("div", { class: "preview" }),
      visual = visualFor(kind, e),
      animation = ["black_ice", "demons"].includes(kind) ? e.animation : "pulse";
    box.append(
      el(
        "h3",
        {},
        "Live preview / " +
          (kind === "black_ice" || kind === "demons"
            ? `${M.display(e.visual)} · ${M.display(e.animation)}`
            : "fixed firmware actor"),
      ),
    );
    const canvas = el("canvas", {
      width: 480,
      height: 270,
      "aria-label": "Live visual preview",
    });
    box.append(canvas);
    const program =
      kind === "enemies"
        ? Array.isArray(e.programs)
          ? e.programs[0]
          : ""
        : "Hellbolt";
    const player = P.attach(
      canvas,
      visual || "unavailable",
      animation,
      program || "",
    );
    state.previews.push(player);
    const controls = el("div", { class: "row" });
    for (const mode of ["idle", "reveal", "attack", "replay"])
      controls.append(button(mode.toUpperCase(), () => player.play(mode)));
    box.append(controls);
    box.append(
      el(
        "p",
        { class: "hint" },
        kind === "black_ice"
          ? "Firmware primitives · 6 × 70 ms reveal + 450 ms hold · 8 × 45 ms attack. Canvas edges may differ from LCD rasterization."
          : kind === "demons"
            ? "Firmware-aligned Demon visual and animation preview. Replay uses the selected attack style."
            : "Fixed bust; no public visual or animation fields. Attack previews the first listed program (generic pulse if no program). This does not simulate AI.",
      ),
    );
    parent.append(box);
  }
  function entityEditor(parent, e, kind, path) {
    if (!M.object(e)) {
      parent.append(
        el("p", { class: "hint" }, "Invalid definition: repair in JSON."),
      );
      return;
    }
    const identity = el("div", { class: "grid" });
    string(identity, e, "id", path, 31);
    string(
      identity,
      e,
      kind === "enemies" ? "handle" : "name",
      path,
      kind === "enemies" ? 15 : 31,
    );
    parent.append(el("h3", {}, "Identity"), identity);
    const stats = el("div", { class: "grid" });
    parent.append(el("h3", {}, "Stats"), stats);
    if (kind === "black_ice") {
      for (const k of ["per", "spd", "atk", "def"])
        num(stats, e, k, 1, 10, path);
      num(stats, e, "rez", 1, 99, path);
      parent.append(el("h3", {}, "Effect / behavior"));
      field(parent, e, "behavior", {
        path: path + ".behavior",
        values: D.behaviors,
      });
      const effects = el("div", { class: "grid" });
      parent.append(effects);
      for (const k of [
        "damage_dice",
        "net_action_penalty",
        "minimum_actions",
        "status_amount",
      ])
        num(effects, e, k, 0, 255, path, true);
      parent.append(
        el(
          "p",
          { class: "hint" },
          "Damage behaviors: 1–6 dice. Destroy / fire: 0 or omitted. Action penalty: 1–3 penalty and 1–5 minimum actions. Move / stat penalty: 1–3 status amount. No automatic effect changes.",
        ),
      );
      field(parent, e, "player_usable", {
        path: path + ".player_usable",
        type: "boolean",
      });
      const presentation = el("div", { class: "grid" });
      parent.append(el("h3", {}, "Presentation"), presentation);
      field(presentation, e, "visual", {
        path: path + ".visual",
        values: D.visuals,
        labels: Object.fromEntries(
          D.visuals.map((v) => [v, `${D.sprites[v].label} / ${v}`]),
        ),
      });
      field(presentation, e, "animation", {
        path: path + ".animation",
        values: D.animations,
      });
      const swatches = el("div", { class: "swatches" });
      for (const id of D.visuals) {
        const c = el("canvas", { width: 56, height: 56, "aria-label": id });
        P.thumbnail(c, id);
        const b = button(
          "",
          () =>
            mutate(() => {
              e.visual = id;
            }),
          "swatch" + (e.visual === id ? " selected" : ""),
        );
        b.setAttribute("aria-label", "Select visual " + id);
        b.append(c, el("span", {}, D.sprites[id].label), el("code", {}, id));
        swatches.append(b);
      }
      parent.append(swatches);
    } else {
      num(stats, e, "interface", 1, 10, path);
      num(
        stats,
        e,
        kind === "demons" ? "rez" : "hp",
        1,
        kind === "demons" ? 2147483647 : 255,
        path,
      );
      num(stats, e, "actions", 1, 5, path);
      if (kind === "demons") {
        const presentation = el("div", { class: "grid" }); parent.append(el("h3", {}, "Presentation"), presentation);
        field(presentation, e, "visual", { path: path + ".visual", values: D.demonVisuals.map((v) => v.id), labels: Object.fromEntries(D.demonVisuals.map((v) => [v.id, `${v.label} / ${v.id}`])) });
        field(presentation, e, "animation", { path: path + ".animation", values: D.demonAnimations });
      }
      if (kind === "enemies") {
        field(parent, e, "behavior", {
          path: path + ".behavior",
          values: M.behaviors,
          optional: true,
          default: "baseline",
          help: "Omitted/null = baseline. Defensive prioritizes valid Defender activation. Sentry suppresses remote chase; baseline combat when co-located.",
        });
        field(parent, e, "ai", {
          path: path + ".ai",
          values: ["anti_personnel"],
          optional: true,
        });
        field(parent, e, "dormant_until_discovered", {
          path: path + ".dormant_until_discovered",
          type: "boolean",
          optional: true,
          default: false,
        });
        listEditor(
          parent,
          e,
          "programs",
          path,
          D.programs.map((p) => p.name),
        );
        parent.append(
          el(
            "p",
            { class: "hint" },
            "Standard deck: 7 slots. Up to 9 program entries, each current program costs 1 slot. Duplicate programs are supported.",
          ),
        );
      }
    }
    preview(parent, kind, e);
  }
  function entriesEditor(parent, doc, kind, path, selectionKey) {
    const entries = doc[kind];
    if (!Array.isArray(entries)) {
      parent.append(
        el("p", { class: "hint" }, "Invalid array: use JSON repair."),
        button("Initialize definitions", () =>
          mutate(() => {
            doc[kind] = [];
          }),
        ),
      );
      return;
    }
    const local = path !== "$";
    parent.append(
      el(
        "p",
        { class: "count" },
        `${entries.length} / ${local && kind === "enemies" ? 2 : 8} definitions · ${local ? "LOCAL / PORTABLE" : "GLOBAL CATALOG"}`,
      ),
    );
    const selected = Math.min(
      state.selected[selectionKey] || 0,
      Math.max(0, entries.length - 1),
    );
    state.selected[selectionKey] = selected;
    const tabs = el("div", { class: "entry-tabs" });
    entries.forEach((e, i) => {
      const b = button(
        e?.id || `Entry ${i + 1}`,
        () => {
          if (canEdit()) {
            state.selected[selectionKey] = i; scheduleSave();
            render();
          }
        },
        selected === i ? "selected" : "",
      );
      state.summaries.push(() => {
        b.textContent = M.display(e?.id || `Entry ${i + 1}`);
      });
      tabs.append(b);
    });
    parent.append(tabs);
    parent.append(
      button("Create " + names[kind], () =>
        mutate(() => {
          entries.push(
            M.entry(
              kind,
              uniqueId(entries, local ? "local_" + kind : "custom_" + kind),
            ),
          );
          state.selected[selectionKey] = entries.length - 1;
        }),
      ),
    );
    if (!entries.length) {
      parent.append(
        el(
          "p",
          { class: "empty" },
          "No definitions. Create one or import a catalog.",
        ),
      );
      return;
    }
    const e = entries[selected];
    parent.append(
      el(
        "div",
        { class: "row" },
        button("Duplicate definition", () =>
          mutate(() => {
            const copy = M.clone(e);
            if (M.object(copy))
              copy.id = uniqueId(
                entries,
                (typeof copy.id === "string" ? copy.id : "copy") + "_copy",
              );
            entries.push(copy);
            state.selected[selectionKey] = entries.length - 1;
          }),
        ),
        button(
          "Delete definition",
          () =>
            remove(
              () => entries.splice(selected, 1),
              "Delete this definition? References will be retained and validated. You cannot undo this action.",
            ),
          "danger",
        ),
      ),
    );
    entityEditor(parent, e, kind, `${path}.${kind}[${selected}]`);
  }
  function drawGraphLegacy(parent, floors) {
    const good = floors.filter(M.object),
      g = M.graph(good),
      svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute(
      "viewBox",
      `0 0 600 ${Math.max(110, Math.ceil(good.length / 4) * 85 + 25)}`,
    );
    svg.setAttribute("class", "graph");
    svg.setAttribute("role", "img");
    svg.setAttribute(
      "aria-label",
      "Undirected architecture graph, first floor is entry",
    );
    const positions = new Map(
      good.map((f, i) => [
        f.id,
        { x: 75 + (i % 4) * 150, y: 50 + Math.floor(i / 4) * 85 },
      ]),
    );
    function s(tag, attrs, text) {
      const n = document.createElementNS(svg.namespaceURI, tag);
      Object.entries(attrs).forEach(([k, v]) => n.setAttribute(k, v));
      if (text) n.textContent = text;
      svg.append(n);
    }
    for (const [id, edges] of g.nodes)
      for (const to of edges)
        if (M.display(id) < M.display(to)) {
          const a = positions.get(id),
            b = positions.get(to);
          s("line", {
            x1: a.x,
            y1: a.y,
            x2: b.x,
            y2: b.y,
            stroke: "#875879",
            "stroke-width": 2,
          });
        }
    good.forEach((f, i) => {
      const { x, y } = positions.get(f.id);
      s("rect", {
        x: x - 53,
        y: y - 23,
        width: 106,
        height: 46,
        rx: 6,
        fill: "#24182e",
        stroke: i === 0 ? "#81d5ad" : "#ed66cf",
      });
      s(
        "text",
        { x, y: y - 3, fill: "#fff", "font-size": 13, "text-anchor": "middle" },
        `${i === 0 ? "ENTRY " : ""}${M.display(f.id)}`,
      );
      s(
        "text",
        {
          x,
          y: y + 14,
          fill: "#c7adc5",
          "font-size": 11,
          "text-anchor": "middle",
        },
        M.display(f.type || "?"),
      );
    });
    parent.append(
      svg,
      el(
        "p",
        { class: "hint" },
        "Edges are undirected after firmware normalization. First floor is entry. Reordering changes entry and implicit linear order; IDs and explicit links are retained.",
      ),
    );
  }
  function scenarioLegacy(parent, doc) {
    const meta = el("section");
    meta.append(el("h2", {}, "Architecture / identity"));
    const grid = el("div", { class: "grid" });
    string(grid, doc, "id", "$", 31);
    string(grid, doc, "name", "$", 39);
    meta.append(grid);
    string(meta, doc, "description", "$", 79, true);
    referenceField(meta, doc, "demon", "demons", "$");
    parent.append(meta);
    const floorBox = el("section");
    floorBox.append(el("h2", {}, "Floors & connections"));
    parent.append(floorBox);
    if (!Array.isArray(doc.floors)) {
      floorBox.append(el("p", {}, "Invalid floors: repair in JSON."));
      return;
    }
    const floors = doc.floors;
    const graphBox = el("div");
    floorBox.append(graphBox);
    state.summaries.push(() => {
      graphBox.replaceChildren();
      drawGraph(graphBox, floors);
    });
    const explicit = floors.some((f) => M.object(f) && M.has(f, "next"));
    floorBox.append(
      button(
        explicit ? "Use implicit linear order" : "Enable explicit connections",
        () =>
          remove(
            () => {
              if (explicit)
                floors.forEach((f) => {
                  if (M.object(f)) delete f.next;
                });
              else
                floors.forEach((f, i) => {
                  if (M.object(f))
                    f.next = i + 1 < floors.length ? [floors[i + 1]?.id] : [];
                });
            },
            explicit
              ? "Remove ALL explicit links and use list order?"
              : "Initialize explicit links from the current linear order?",
          ),
      ),
    );
    floorBox.append(
      el(
        "p",
        { class: "count" },
        `${floors.length} / 16 floors · ${explicit ? "EXPLICIT GRAPH" : "IMPLICIT LINE"}`,
      ),
    );
    const selected = Math.min(
      state.selected.scenario,
      Math.max(0, floors.length - 1),
    );
    state.selected.scenario = selected;
    const tabs = el("div", { class: "entry-tabs" });
    floors.forEach((f, i) => {
      const b = button(
        `${M.display(f?.id ?? "?")} / ${M.display(f?.type || "invalid")}`,
        () => {
          if (canEdit()) {
            state.selected.scenario = i; scheduleSave();
            render();
          }
        },
        i === selected ? "selected" : "",
      );
      state.summaries.push(() => {
        b.textContent = `${M.display(f?.id ?? "?")} / ${M.display(f?.type || "invalid")}`;
      });
      tabs.append(b);
    });
    floorBox.append(tabs);
    floorBox.append(
      button("Add floor", () =>
        mutate(() => {
          let id = 0;
          while (floors.some((f) => f?.id === id)) id++;
          floors.push({ id, type: "empty", ...(explicit ? { next: [] } : {}) });
          state.selected.scenario = floors.length - 1;
        }),
      ),
    );
    if (floors.length) {
      const f = floors[selected],
        p = `$.floors[${selected}]`;
      const actions = el("div", { class: "row" });
      for (const [text, delta] of [
        ["Move earlier", -1],
        ["Move later", 1],
      ]) {
        const b = button(text, () =>
          mutate(() => {
            const to = selected + delta;
            [floors[to], floors[selected]] = [floors[selected], floors[to]];
            state.selected.scenario = to;
          }),
        );
        b.disabled = selected + delta < 0 || selected + delta >= floors.length;
        actions.append(b);
      }
      actions.append(
        button(
          "Delete floor",
          () =>
            remove(
              () => floors.splice(selected, 1),
              "Delete this floor? Incoming links remain visible as validation errors.",
            ),
          "danger",
        ),
      );
      floorBox.append(actions);
      if (M.object(f)) {
        const fg = el("div", { class: "grid" });
        num(fg, f, "id", 0, 255, p);
        field(fg, f, "type", {
          path: p + ".type",
          values: M.floorTypes,
          help: "Changing type retains other fields. Initialize new type data below.",
        });
        floorBox.append(fg);
        if (["password", "file", "control", "black_ice"].includes(f.type))
          floorBox.append(
            button("Initialize missing type fields", () =>
              mutate(() => {
                if (
                  ["password", "file", "control"].includes(f.type) &&
                  !M.has(f, "dv")
                )
                  f.dv = 6;
                if (f.type === "password" && !M.has(f, "security"))
                  f.security = "low";
                if (f.type === "file" && !M.has(f, "file"))
                  f.file = { name: "DATA.LOG", type: "LOG", value: 0 };
                if (f.type === "control" && !M.has(f, "control"))
                  f.control = { name: "NODE" };
                if (f.type === "black_ice" && !M.has(f, "ice")) f.ice = [];
              }),
            ),
          );
        num(
          floorBox,
          f,
          "dv",
          0,
          255,
          p,
          !["password", "file", "control"].includes(f.type),
        );
        if (f.type === "password" || M.has(f, "security"))
          field(floorBox, f, "security", {
            path: p + ".security",
            values: ["low", "medium", "high"],
            optional: f.type !== "password",
          });
        string(floorBox, f, "content_id", p, 31, true);
        string(floorBox, f, "description", p, 79, true);
        for (const type of ["file", "control"])
          if (M.object(f[type])) {
            const box = el("div", { class: "card" });
            box.append(el("h3", {}, type));
            string(box, f[type], "name", p + "." + type, 39);
            if (type === "file") {
              string(box, f.file, "type", p + ".file", 23);
              num(box, f.file, "value", 0, 4294967295, p + ".file");
            } else
              string(box, f.control, "description", p + ".control", 79, true);
            box.append(
              button(
                "Remove " + type + " data",
                () =>
                  remove(
                    () => delete f[type],
                    "Remove this type-specific data?",
                  ),
                "danger",
              ),
            );
            floorBox.append(box);
          }
        if (f.type === "black_ice" || M.has(f, "ice")) {
          listEditor(floorBox, f, "ice", p);
          floorBox.append(
            button(
              "Remove ice data",
              () =>
                remove(
                  () => delete f.ice,
                  "Remove all ICE placement data from this floor?",
                ),
              "danger",
            ),
          );
          floorBox
            .querySelectorAll(`input[data-path^="${p}.ice["]`)
            .forEach((n) => n.setAttribute("list", "refs-black_ice"));
          const placements = el("div");
          floorBox.append(placements);
          state.summaries.push(() => {
            placements.replaceChildren();
            if (Array.isArray(f.ice))
              f.ice.forEach((id) => placement(placements, "black_ice", id));
          });
        }
        referenceField(floorBox, f, "enemy", "enemies", p);
        if (explicit) {
          listEditor(floorBox, f, "next", p, null, true);
          const links = el("div", { class: "connections" });
          if (Array.isArray(f.next))
            floors
              .filter((t) => M.object(t) && t.id !== f.id)
              .forEach((t) => {
                const check = el("input", {
                  type: "checkbox",
                  "aria-label": `Connect floor ${M.display(f.id)} to ${M.display(t.id)}`,
                });
                check.checked = f.next.includes(t.id);
                check.addEventListener("change", () =>
                  mutate(() => {
                    if (check.checked) f.next.push(t.id);
                    else f.next = f.next.filter((id) => id !== t.id);
                  }),
                );
                links.append(
                  el("label", {}, check, `Floor ${M.display(t.id)}`),
                );
              });
          floorBox.append(links);
        }
      } else floorBox.append(el("p", {}, "Invalid floor: repair in JSON."));
    }
    const local = el("details", { "data-detail": "locals" });
    if (M.kinds.slice(1).some((k) => M.has(doc, k))) local.open = true;
    local.append(
      el("summary", {}, "Advanced / LOCAL & PORTABLE definitions"),
      el(
        "p",
        { class: "hint" },
        "Default workflow uses separate catalogs. Locals embed definitions in this scenario only. Do not keep the same ID in a global SD catalog. Enabling does not copy any global definitions.",
      ),
    );
    for (const kind of M.kinds.slice(1)) {
      const sub = el("details", { "data-detail": "local_" + kind });
      sub.append(el("summary", {}, names[kind] + " / local"));
      if (!Array.isArray(doc[kind]))
        sub.append(
          button("Enable local " + names[kind], () =>
            mutate(() => {
              doc[kind] = [];
            }),
          ),
        );
      else {
        entriesEditor(sub, doc, kind, "$", "local_" + kind);
        sub.append(
          button(
            "Remove all local " + names[kind],
            () =>
              remove(
                () => delete doc[kind],
                "Delete ALL local definitions of this type? References will not be removed.",
              ),
            "danger",
          ),
        );
      }
      local.append(sub);
    }
    parent.append(local);
  }
  function referenceSelect(parent, obj, key, kind, path, label) {
    const wrap = el("div", { class: "card placement-editor" });
    wrap.append(el("h3", {}, label));
    const select = el("select", { "data-path": `${path}.${key}`, "aria-label": label });
    const choices = [], seen = new Set();
    for (const [scope, entries] of [["local", M.list(state.docs.scenario, kind)], ["catalog", M.list(catalogs()[kind], kind)], ["builtin", D.builtins[kind]]])
      entries.forEach((entry) => { if (M.object(entry) && typeof entry.id === "string" && !seen.has(entry.id)) { seen.add(entry.id); choices.push([entry.id, `${M.display(entry.name || entry.handle || entry.id)} · ${scope}`]); } });
    choices.forEach(([id, text]) => select.append(el("option", { value: id }, text)));
    select.value = obj[key] || choices[0]?.[0] || "";
    select.addEventListener("change", () => mutate(() => { obj[key] = select.value; }));
    wrap.append(select, button(`Remove ${label}`, () => mutate(() => delete obj[key]), "danger"));
    const previewBox = el("div"); wrap.append(previewBox);
    state.summaries.push(() => { previewBox.replaceChildren(); if (obj[key]) placement(previewBox, kind, obj[key]); });
    parent.append(wrap);
  }
  function graphPosition(id, index) {
    return state.graphPositions[id] || (state.graphPositions[id] = { x: 85 + (index % 4) * 150, y: 55 + Math.floor(index / 4) * 90 });
  }
  function removeGraphEdge(floors, a, b) {
    const parsed = M.parseGraphText(M.graphText(floors), floors);
    M.setGraphEdges(floors, parsed.edges.filter((edge) => !(edge.includes(a) && edge.includes(b))));
  }
  function drawGraph(parent, floors) {
    const good = floors.filter(M.object), g = M.graph(good), height = Math.max(145, Math.ceil(good.length / 4) * 90 + 45);
    const svg = document.createElementNS("http:" + "//www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", `0 0 650 ${height}`); svg.setAttribute("class", "graph interactive-graph"); svg.setAttribute("aria-label", "Interactive architecture graph");
    const make = (tag, attrs, text) => { const node = document.createElementNS(svg.namespaceURI, tag); Object.entries(attrs).forEach(([k, v]) => node.setAttribute(k, v)); if (text) node.textContent = text; svg.append(node); return node; };
    const positions = new Map(good.map((f, i) => [f.id, graphPosition(f.id, i)]));
    for (const [a, edges] of g.nodes) for (const b of edges) if (Number(a) < Number(b)) { const x = positions.get(a), y = positions.get(b); const line = make("line", { x1: x.x, y1: x.y, x2: y.x, y2: y.y, class: "graph-edge", "data-edge-a": a, "data-edge-b": b, tabindex: "0" }); line.addEventListener("click", (e) => { e.stopPropagation(); mutate(() => removeGraphEdge(floors, a, b)); }); line.setAttribute("aria-label", `Remove connection Floor ${a} to Floor ${b}`); }
    let drag = null, connect = null, moved = false;
    const selectFloor = (index) => { state.selected.scenario = index; scheduleSave(); render(); document.querySelector(".floor-definition")?.scrollIntoView({ block: "nearest" }); };
    good.forEach((f, index) => { const pos = positions.get(f.id), node = make("g", { class: `graph-node ${state.selected.scenario === index ? "selected" : ""}`, transform: `translate(${pos.x} ${pos.y})`, "data-floor-id": f.id, tabindex: "0" }); const rect = document.createElementNS(svg.namespaceURI, "rect"); rect.setAttribute("x", "-55"); rect.setAttribute("y", "-25"); rect.setAttribute("width", "110"); rect.setAttribute("height", "50"); rect.setAttribute("rx", "7"); node.append(rect); const text = document.createElementNS(svg.namespaceURI, "text"); text.setAttribute("text-anchor", "middle"); text.setAttribute("y", "-4"); text.textContent = `FLOOR ${f.id}`; node.append(text); const sub = document.createElementNS(svg.namespaceURI, "text"); sub.setAttribute("text-anchor", "middle"); sub.setAttribute("y", "14"); sub.setAttribute("class", "graph-sub"); sub.textContent = M.display(f.type); node.append(sub); const handle = document.createElementNS(svg.namespaceURI, "circle"); handle.setAttribute("cx", "50"); handle.setAttribute("cy", "-20"); handle.setAttribute("r", "7"); handle.setAttribute("class", "connection-handle"); node.append(handle); handle.addEventListener("pointerdown", (e) => { e.stopPropagation(); connect = f.id; svg.setPointerCapture(e.pointerId); }); node.addEventListener("pointerdown", (e) => { if (e.target === handle) return; drag = { id: f.id, pointer: e.pointerId }; moved = false; svg.setPointerCapture(e.pointerId); }); node.addEventListener("click", () => { if (!moved && connect === null) selectFloor(index); }); svg.append(node); });
    const pointFor = (event) => { const p = svg.createSVGPoint(); p.x = event.clientX; p.y = event.clientY; return p.matrixTransform(svg.getScreenCTM().inverse()); };
    const updateGeometry = () => svg.querySelectorAll("line.graph-edge").forEach((line) => { const a = positions.get(Number(line.dataset.edgeA)), b = positions.get(Number(line.dataset.edgeB)); if (a && b) { line.setAttribute("x1", a.x); line.setAttribute("y1", a.y); line.setAttribute("x2", b.x); line.setAttribute("y2", b.y); } });
    const clearPreview = () => { svg.querySelector(".connection-preview")?.remove(); svg.querySelectorAll(".connection-target").forEach((node) => node.classList.remove("connection-target")); };
    svg.addEventListener("pointermove", (e) => { const point = pointFor(e); if (drag) { const pos = { x: Math.max(60, Math.min(590, point.x)), y: Math.max(35, Math.min(height - 25, point.y)) }; state.graphPositions[drag.id] = pos; positions.set(drag.id, pos); const node = svg.querySelector(`[data-floor-id="${drag.id}"]`); if (node) node.setAttribute("transform", `translate(${pos.x} ${pos.y})`); updateGeometry(); moved = true; state.dirty = true; scheduleSave(); return; } if (connect !== null) { clearPreview(); const from = positions.get(connect), preview = make("line", { class: "connection-preview" }); preview.setAttribute("x1", from.x); preview.setAttribute("y1", from.y); preview.setAttribute("x2", point.x); preview.setAttribute("y2", point.y); const target = document.elementFromPoint(e.clientX, e.clientY)?.closest?.("[data-floor-id]"); if (target && Number(target.dataset.floorId) !== connect) target.classList.add("connection-target"); } });
    const finishPointer = (e, cancelled = false) => { if (connect !== null && !cancelled) { const target = document.elementFromPoint(e.clientX, e.clientY)?.closest?.("[data-floor-id]"); const to = target ? Number(target.dataset.floorId) : null; const parsed = M.parseGraphText(M.graphText(floors), floors); if (to !== null && to !== connect && !parsed.issues.length && !parsed.edges.some((edge) => edge.includes(connect) && edge.includes(to))) { const edges = parsed.edges.concat([[connect, to]]), check = M.parseGraphText(edges.map(([a, b]) => `${a}: ${b}`).join("\n"), floors); if (!check.issues.length) { M.setGraphEdges(floors, edges); state.graphText = M.graphText(floors); state.dirty = true; render(); } else notice(check.issues.map((issue) => issue.message).join(" · ")); } } clearPreview(); drag = null; connect = null; if (svg.hasPointerCapture(e.pointerId)) svg.releasePointerCapture(e.pointerId); };
    svg.addEventListener("pointerup", (e) => finishPointer(e)); svg.addEventListener("pointercancel", (e) => finishPointer(e, true));
    parent.append(svg, el("p", { class: "hint" }, "Drag a Floor card to arrange it. Drag from the small dot to another Floor to connect. Click a line to remove that connection."));
  }
  function scenario(parent, doc) {
    const meta = el("section"); meta.append(el("h2", {}, "Architecture / identity")); const grid = el("div", { class: "grid" }); string(grid, doc, "id", "$", 31); string(grid, doc, "name", "$", 39); meta.append(grid); string(meta, doc, "description", "$", 79, true);
    if (M.has(doc, "demon")) referenceSelect(meta, doc, "demon", "demons", "$", "Demon"); else meta.append(el("div", { class: "card" }, el("h3", {}, "Demon"), el("p", { class: "hint" }, "Current device schema places a Demon at architecture scope, not on an individual Floor."), button("ADD DEMON", () => mutate(() => { doc.demon = D.builtins.demons[0]?.id || ""; })))); parent.append(meta);
    const box = el("section", { class: "floor-definition" }); box.append(el("h2", {}, "Floors & connections")); parent.append(box); if (!Array.isArray(doc.floors)) { box.append(el("p", {}, "Malformed Floors array: repair the imported JSON.")); return; }
    const floors = doc.floors, selected = Math.min(state.selected.scenario, Math.max(0, floors.length - 1)); state.selected.scenario = selected;
    const graphBox = el("div", { class: "graph-box" }); box.append(graphBox); state.summaries.push(() => { graphBox.replaceChildren(); drawGraph(graphBox, floors); });
    const graphEditor = el("label", { class: "field" }, el("span", { class: "field-name" }, "Connection map")); const graphInput = el("textarea", { class: "graph-text", "aria-label": "Connection map", placeholder: "1: 2, 3\n2: 4" }); graphInput.value = state.graphText || M.graphText(floors); graphInput.addEventListener("input", () => { state.graphText = graphInput.value; const parsed = M.parseGraphText(graphInput.value, floors); state.graphIssues = parsed.issues; if (!parsed.issues.length) { M.setGraphEdges(floors, parsed.edges); state.dirty = true; scheduleSave(); graphBox.replaceChildren(); drawGraph(graphBox, floors); validate(); } else validate(); }); graphEditor.append(graphInput, el("small", {}, "One logical connection per pair. Reciprocal device links are normalized automatically.")); box.append(graphEditor);
    const graphErrors = el("div", { class: "field-error graph-errors" }); box.append(graphErrors); state.summaries.push(() => { graphErrors.replaceChildren(...state.graphIssues.map((i) => el("p", {}, `Line ${i.line || "map"}: ${i.message}`))); if (!state.graphIssues.length) state.graphText = M.graphText(floors); });
    box.append(el("p", { class: "count" }, `${floors.length} / 16 floors · ${M.graph(floors).explicit ? "CUSTOM ROUTES" : "SEQUENTIAL ROUTE"}`)); const tabs = el("div", { class: "entry-tabs" }); floors.forEach((f, i) => tabs.append(button(`Floor ${M.display(f?.id ?? "?")}`, () => { state.selected.scenario = i; render(); }, i === selected ? "selected" : ""))); box.append(tabs);
    box.append(button("Add floor", () => mutate(() => { let id = 0; while (floors.some((f) => f?.id === id)) id++; const explicit = M.graph(floors).explicit; const floor = M.floorForType(id, "empty"); if (explicit) { floor.next = []; const prior = floors[floors.length - 1]; if (prior) { const parsed = M.parseGraphText(M.graphText(floors), floors); parsed.edges.push([prior.id, id]); floors.push(floor); M.setGraphEdges(floors, parsed.edges); } else floors.push(floor); } else floors.push(floor); state.selected.scenario = floors.length - 1; })));
    const f = floors[selected], path = `$.floors[${selected}]`; if (!M.object(f)) return; const actions = el("div", { class: "row" }, button("Delete Floor", () => remove(() => floors.splice(selected, 1), "Delete this Floor?"), "danger")); box.append(actions); const fg = el("div", { class: "grid" }); num(fg, f, "id", 0, 255, path); const neutral = el("label", { class: "field" }, el("span", { class: "field-name" }, "Neutral floor object")); const type = el("select", { "data-path": `${path}.type`, "aria-label": "Neutral floor object" }); M.neutralTypes.forEach((value) => type.append(el("option", { value }, value === "empty" ? "None" : M.display(value)))); if (!M.neutralTypes.includes(f.type)) type.value = "empty"; else type.value = f.type; type.addEventListener("change", () => mutate(() => M.setFloorType(f, type.value))); neutral.append(type); fg.append(neutral); box.append(fg);
    if (["password", "file", "control"].includes(f.type)) num(box, f, "dv", 0, 255, path); if (f.type === "password") field(box, f, "security", { path: path + ".security", values: ["low", "medium", "high"], label: "Password security" }); if (f.type === "file") { const card = el("div", { class: "card" }, el("h3", {}, "File")); string(card, f.file, "name", path + ".file", 39); string(card, f.file, "type", path + ".file", 23); num(card, f.file, "value", 0, 4294967295, path + ".file"); box.append(card); } if (f.type === "control") { const card = el("div", { class: "card" }, el("h3", {}, "Control Node")); string(card, f.control, "name", path + ".control", 39); string(card, f.control, "description", path + ".control", 79, true); box.append(card); }
    const ice = el("div", { class: "card" }, el("h3", {}, "Black ICE")); { const list = Array.isArray(f.ice) ? f.ice : []; ice.append(el("p", { class: "count" }, `${list.length} / ${M.limits.icePerFloor} per Floor`)); list.forEach((id, i) => { const row = el("div", { class: "row" }); const select = el("select", { "data-path": `${path}.ice[${i}]`, "aria-label": `Black ICE ${i + 1}` }); const seen = new Set(); [D.builtins.black_ice, M.list(catalogs().black_ice, "black_ice"), M.list(doc, "black_ice")].flat().forEach((entry) => { if (entry?.id && !seen.has(entry.id)) { seen.add(entry.id); select.append(el("option", { value: entry.id }, `${M.display(entry.name || entry.id)} · ${entry.id}`)); } }); select.value = id; select.addEventListener("change", () => mutate(() => list[i] = select.value)); const preview = el("canvas", { class: "ice-slot-preview", width: 42, height: 42, "aria-label": `Black ICE ${i + 1} preview` }); const found = M.resolve("black_ice", id, state.docs.scenario, catalogs()); if (found) P.thumbnail(preview, visualFor("black_ice", found.entry)); row.append(select, preview, button("Remove", () => mutate(() => { list.splice(i, 1); if (!list.length) delete f.ice; }), "danger")); ice.append(row); }); const add = button("ADD BLACK ICE", () => mutate(() => { if (!Array.isArray(f.ice)) f.ice = []; if (f.ice.length >= M.limits.icePerFloor) { notice(`Black ICE limit: ${M.limits.icePerFloor} per Floor.`); return; } f.ice.push(D.builtins.black_ice[0]?.id || ""); })); add.disabled = list.length >= M.limits.icePerFloor; ice.append(add); } box.append(ice);
    if (M.has(f, "enemy")) referenceSelect(box, f, "enemy", "enemies", path, "Enemy Netrunner"); else box.append(el("div", { class: "card" }, el("h3", {}, "Enemy Netrunner"), button("ADD ENEMY NETRUNNER", () => mutate(() => { f.enemy = D.builtins.enemies[0]?.id || ""; }))));
    const local = el("details", { "data-detail": "locals" });
    if (M.kinds.slice(1).some((kind) => M.has(doc, kind))) local.open = true;
    local.append(el("summary", {}, "Advanced / LOCAL & PORTABLE definitions"), el("p", { class: "hint" }, "Local definitions are embedded in this Scenario and remain optional."));
    for (const kind of M.kinds.slice(1)) {
      const sub = el("details", { "data-detail": "local_" + kind }); sub.append(el("summary", {}, names[kind] + " / local"));
      if (!Array.isArray(doc[kind])) sub.append(button("Enable local " + names[kind], () => mutate(() => { doc[kind] = []; })));
      else { entriesEditor(sub, doc, kind, "$", "local_" + kind); sub.append(button("Remove all local " + names[kind], () => remove(() => delete doc[kind], "Delete all local definitions?"), "danger")); }
      local.append(sub);
    }
    parent.append(local);
  }
  function humanIssue(issue) {
    const floor = issue.path.match(/^\$\.floors\[(\d+)\](?:\.(file|control|ice|enemy|security|dv))?/);
    if (floor) { const id = state.docs.scenario?.floors?.[Number(floor[1])]?.id ?? Number(floor[1]) + 1; const label = { file: "File", control: "Control Node", ice: "Black ICE", enemy: "Enemy Netrunner", security: "Password", dv: "Floor object" }[floor[2]] || "Floor"; return `Floor ${id} – ${label}: ${issue.message}`; }
    if (/^\$\.(black_ice|demons|enemies)/.test(issue.path)) return `${names[state.kind] || "Catalog"}: ${issue.message}`;
    return issue.message;
  }
  function validate() {
    state.summaries.forEach((update) => update());
    const doc = state.docs[state.kind],
      issues = M.validate(doc, state.kind, catalogs()),
      errors = issues.filter((i) => i.severity === "error");
    $("destination").textContent = M.destination(doc, state.kind);
    $("download").disabled = errors.length > 0 || state.rawDirty;
    $("validation-title").textContent = state.rawDirty
      ? "Unapplied JSON edits"
      : errors.length
        ? `${errors.length} export-blocking errors`
        : issues.length
          ? "Valid with offline warnings"
          : "Ready to export";
    $("issues").replaceChildren(
      ...(issues.length
        ? issues.map((i) =>
            el("li", { class: i.severity }, button(humanIssue(i), () => { const match = i.path.match(/^\$\.floors\[(\d+)\]/); if (match) { state.selected.scenario = Number(match[1]); render(); } }), el("small", { class: "hint" }, i.path)),
          )
        : [
            el("li", { class: "success" }, "All known contract checks passed."),
          ]),
    );
    document
      .querySelectorAll("[data-path]")
      .forEach((n) =>
        n.setAttribute("aria-invalid", String(errors.some((i) => i.path === n.dataset.path || i.path.startsWith(n.dataset.path + ".")))),
      );
    $("memory").textContent =
      `ESP32 JSON pool estimate: ${M.memoryUsage(doc).bytes} / ${M.limits.json[state.kind]} bytes. Catalog availability: ${M.kinds
        .slice(1)
        .map((k) => `${k} ${state.loaded[k] ? "loaded" : "unknown"}`)
        .join(" · ")}`;
    if (!state.rawDirty)
      $("json").value = JSON.stringify(M.stable(doc), null, 2) + "\n";
  }
  function render() {
    document.querySelectorAll("details[data-detail]").forEach((d) => {
      state.details[d.dataset.detail] = d.open;
    });
    state.previews.forEach((p) => p.dispose());
    state.previews = [];
    state.summaries = [];
    const nav = $("navigation");
    nav.replaceChildren();
    for (const k of M.kinds)
      nav.append(
        button(
          names[k],
          () => {
            if (canEdit()) {
              state.kind = k; scheduleSave();
              render();
            }
          },
          state.kind === k ? "selected" : "",
        ),
      );
    nav.append(
      el(
        "p",
        {},
        "Scenario: schemaVersion 1",
        el("br"),
        "Catalogs: version 1",
        el("br"),
        "No legacy schema selector.",
      ),
    );
    if (state.kind !== "scenario") {
      const k = state.kind;
      nav.append(
        button(
          state.loaded[k]
            ? "Mark catalog unavailable"
            : "Confirm catalog loaded",
          () => {
            if (canEdit()) {
              state.loaded[k] = !state.loaded[k]; scheduleSave();
              render();
            }
          },
        ),
      );
    }
    $("new").textContent = state.kind === "scenario" ? "New Scenario" : "New Catalog";
    const main = $("editor");
    main.replaceChildren();
    const doc = state.docs[state.kind];
    for (const kind of M.kinds.slice(1)) {
      const datalist = el("datalist", { id: "refs-" + kind }),
        seen = new Set();
      for (const [scope, entries] of [
        ["local", M.list(state.docs.scenario, kind)],
        ["catalog", M.list(catalogs()[kind], kind)],
        ["builtin", D.builtins[kind]],
      ])
        for (const e of entries)
          if (M.object(e) && typeof e.id === "string" && !seen.has(e.id)) {
            datalist.append(
              el(
                "option",
                { value: e.id },
                `${M.display(e.name || e.handle || e.id)} / ${scope}`,
              ),
            );
            seen.add(e.id);
          }
      main.append(datalist);
    }
    if (!M.object(doc))
      main.append(el("p", {}, "Invalid document: repair in JSON."));
    else if (state.kind === "scenario") scenario(main, doc);
    else {
      const section = el("section");
      section.append(el("h2", {}, names[state.kind] + " / catalog"));
      entriesEditor(section, doc, state.kind, "$", state.kind);
      main.append(section);
    }
    document.querySelectorAll("details[data-detail]").forEach((d) => {
      if (M.has(state.details, d.dataset.detail))
        d.open = state.details[d.dataset.detail];
    });
    validate();
    scheduleSave();
  }
  function acceptText(text) {
    try {
      const imported = M.parse(text);
      state.docs[imported.kind] = imported.doc;
      state.kind = imported.kind;
      state.selected[imported.kind] = 0;
      if (imported.kind !== "scenario") state.loaded[imported.kind] = true;
      state.rawDirty = false;
      state.dirty = true;
      scheduleSave(); notice("Imported without normalization. Check validation before export.");
      render();
      return true;
    } catch (e) {
      notice("Import failed: " + e.message);
      return false;
    }
  }
  $("clear-workspace").addEventListener("click", () => {
    if (!confirm("Clear the saved local workspace and all catalog documents?")) return;
    clearTimeout(saveTimer);
    let storageCleared = true;
    try {
      localStorage.removeItem(WORKSPACE_STORAGE_KEY);
    } catch (e) {
      storageCleared = false;
    }
    state.docs = Object.fromEntries(M.kinds.map((k) => [k, M.blank(k)]));
    state.loaded = { black_ice: false, demons: false, enemies: false };
    state.selected = { scenario: 0, black_ice: 0, demons: 0, enemies: 0 };
    state.kind = "scenario";
    state.details = {};
    state.graphPositions = {};
    state.graphText = "";
    state.graphIssues = [];
    state.rawDirty = false;
    state.dirty = false;
    state.suppressAutosave = true;
    render();
    state.suppressAutosave = false;
    notice(storageCleared ? "Local workspace cleared" : "Local workspace could not be cleared");
  });
  $("new").addEventListener("click", () => {
    if (
      canEdit() &&
      confirm(
        "Replace the active document with a blank document? Download current work first.",
      )
    ) {
      state.docs[state.kind] = M.blank(state.kind);
      state.selected[state.kind] = 0;
      edited(true);
    }
  });
  $("examples").addEventListener("click", () => {
    if (
      canEdit() &&
      confirm(
        "Replace ALL four documents with the homebrew starter set? Download current work first.",
      )
    ) {
      state.docs = M.starters();
      for (const k of M.kinds.slice(1)) state.loaded[k] = true;
      state.selected = { scenario: 0, black_ice: 0, demons: 0, enemies: 0 };
      edited(true);
      notice("Starter set loaded. Export all four documents separately.");
    }
  });
  $("import").addEventListener("click", () => {
    if (canEdit()) $("file").click();
  });
  $("file").addEventListener("change", async () => {
    const file = $("file").files[0];
    if (!file) return;
    try {
      const text = await file.text();
      const parsed = M.parse(text);
      if (
        confirm(
          `Replace the ${names[parsed.kind]} document with ${file.name}? Other documents are kept.`,
        )
      )
        acceptText(text);
    } catch (e) {
      notice("Import failed: " + e.message);
    } finally {
      $("file").value = "";
    }
  });
  $("json").addEventListener("input", () => {
    state.rawDirty = true;
    state.dirty = true;
    validate();
  });
  $("apply-json").addEventListener("click", () => acceptText($("json").value));
  $("discard-json").addEventListener("click", () => {
    if (!state.rawDirty || confirm("Discard unapplied JSON text edits?")) {
      state.rawDirty = false;
      validate();
      notice("JSON text reset to the active model.");
    }
  });
  $("download").addEventListener("click", () => {
    if (!canEdit()) return;
    try {
      const doc = state.docs[state.kind],
        issues = M.validate(doc, state.kind, catalogs());
      if (
        issues.some((i) => i.severity === "warning") &&
        !confirm(
          "Validation has offline warnings. Export with these unresolved checks?",
        )
      )
        return;
      const text = M.serialize(doc, state.kind, catalogs()),
        url = URL.createObjectURL(
          new Blob([text], { type: "application/json" }),
        ),
        a = el("a", {
          href: url,
          download: M.destination(doc, state.kind).split("/").pop(),
        });
      document.body.append(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      notice("Downloaded. Copy manually to " + M.destination(doc, state.kind));
    } catch (e) {
      notice(e.message);
    }
  });
  window.addEventListener("beforeunload", (e) => {
    if (state.dirty) {
      e.preventDefault();
      e.returnValue = "";
    }
  });
  const restoreMessage = restoreWorkspace();
  render();
  if (restoreMessage) notice(restoreMessage);
})();
