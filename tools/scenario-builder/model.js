(function (root) {
  "use strict";
  const D =
    typeof module === "object" && module.exports
      ? require("./firmware-data.js")
      : root.NRData;
  const kinds = ["scenario", "black_ice", "demons", "enemies"];
  const formats = {
    scenario: "netrun-architecture",
    black_ice: "netrun-black-ice-catalog",
    demons: "netrun-demon-catalog",
    enemies: "netrun-enemy-catalog",
  };
  const limits = {
    floors: 16,
    connections: 4,
    icePerFloor: 3,
    enemies: 2,
    catalog: 8,
    programCount: 9,
    deckSlots: 7,
    json: { scenario: 8192, black_ice: 6144, demons: 4096, enemies: 6144 },
  };
  const fields = {
    black_ice: [
      "id",
      "name",
      "per",
      "spd",
      "atk",
      "def",
      "rez",
      "behavior",
      "damage_dice",
      "net_action_penalty",
      "minimum_actions",
      "status_amount",
      "visual",
      "animation",
      "player_usable",
    ],
    demons: ["id", "name", "interface", "rez", "actions", "visual", "animation"],
    enemies: [
      "id",
      "handle",
      "interface",
      "hp",
      "actions",
      "programs",
      "ai",
      "dormant_until_discovered",
      "behavior",
    ],
    scenario: [
      "format",
      "schemaVersion",
      "id",
      "name",
      "description",
      "demon",
      "black_ice",
      "demons",
      "enemies",
      "floors",
    ],
    floor: [
      "id",
      "type",
      "content_id",
      "description",
      "dv",
      "security",
      "file",
      "control",
      "ice",
      "enemy",
      "next",
    ],
  };
  // `black_ice` is a firmware floor type, not a neutral architecture object.
  const floorTypes = ["empty", "password", "file", "control", "black_ice"];
  const neutralTypes = ["empty", "password", "file", "control"];
  const behaviors = ["baseline", "defensive", "sentry"];
  const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  const object = (o) =>
    o !== null && typeof o === "object" && !Array.isArray(o);
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const bytes = (s) => new TextEncoder().encode(s).length;
  function entry(kind, id) {
    if (kind === "black_ice")
      return {
        id: id || "signal_ice",
        name: "SIGNAL",
        per: 3,
        spd: 4,
        atk: 4,
        def: 3,
        rez: 12,
        behavior: "direct_damage",
        damage_dice: 1,
        visual: "bird",
        animation: "pulse",
        player_usable: true,
      };
    if (kind === "demons")
      return {
        id: id || "relay_demon",
        name: "RELAY",
        interface: 3,
        rez: 15,
        actions: 2,
        visual: "orb",
        animation: "pulse",
      };
    return {
      id: id || "relay_runner",
      handle: "RELAY",
      interface: 3,
      hp: 20,
      actions: 2,
      programs: ["Hellbolt"],
      behavior: "baseline",
    };
  }
  function blank(kind) {
    return kind === "scenario"
      ? {
          format: formats.scenario,
          schemaVersion: 1,
          id: "new_run",
          name: "NEW RUN",
          floors: [{ id: 0, type: "empty" }],
        }
      : { format: formats[kind], version: 1, [kind]: [] };
  }
  function starters() {
    const docs = Object.fromEntries(kinds.map((k) => [k, blank(k)]));
    for (const k of kinds.slice(1)) docs[k][k].push(entry(k));
    docs.scenario = {
      format: formats.scenario,
      schemaVersion: 1,
      id: "signal_relay",
      name: "SIGNAL RELAY",
      description: "Homebrew branch, merge and catalog reference example.",
      demon: "relay_demon",
      floors: [
        { id: 0, type: "password", dv: 6, security: "low", next: [1, 2] },
        {
          id: 1,
          type: "black_ice",
          ice: ["signal_ice"],
          enemy: "relay_runner",
          next: [3],
        },
        {
          id: 2,
          type: "file",
          dv: 6,
          file: { name: "RELAY.LOG", type: "LOG", value: 10 },
          next: [3],
        },
        {
          id: 3,
          type: "control",
          dv: 6,
          control: { name: "RELAY NODE", description: "Branch merge." },
          next: [],
        },
      ],
    };
    return docs;
  }
  function list(doc, key) {
    return object(doc) && Array.isArray(doc[key]) ? doc[key] : [];
  }
  function resolve(kind, id, scenario, catalogs = {}) {
    const local = list(scenario, kind).find((e) => object(e) && e.id === id);
    if (local) return { entry: local, scope: "local" };
    const global = list(catalogs[kind], kind).find(
      (e) => object(e) && e.id === id,
    );
    if (global) return { entry: global, scope: "catalog" };
    const builtin = D.builtins[kind].find((e) => e.id === id);
    return builtin ? { entry: builtin, scope: "builtin" } : null;
  }
  function graph(floors) {
    const nodes = new Map(floors.filter(object).map((f) => [f.id, new Set()]));
    const explicit = floors.some(
      (f) => Array.isArray(f?.next) && f.next.length > 0,
    );
    const connect = (a, b) => {
      if (nodes.has(a) && nodes.has(b)) {
        nodes.get(a).add(b);
        nodes.get(b).add(a);
      }
    };
    if (explicit)
      floors.forEach((f) => {
        if (Array.isArray(f?.next)) f.next.forEach((id) => connect(f.id, id));
      });
    else
      for (let i = 1; i < floors.length; i++)
        connect(floors[i - 1]?.id, floors[i]?.id);
    return { nodes, explicit };
  }
  function floorForType(id, type) {
    const floor = { id, type }; if (["password", "file", "control"].includes(type)) floor.dv = 6;
    if (type === "password") floor.security = "low"; if (type === "file") floor.file = { name: "DATA.LOG", type: "LOG", value: 0 }; if (type === "control") floor.control = { name: "NODE" }; if (type === "black_ice") floor.ice = []; return floor;
  }
  function setFloorType(floor, type) { const neutralKeys = new Set(["type", "dv", "security", "file", "control"]); const preserved = {}; Object.keys(floor).forEach((key) => { if (!neutralKeys.has(key)) preserved[key] = clone(floor[key]); }); preserved.id = floor.id; const fresh = floorForType(preserved.id, type); Object.keys(floor).forEach((key) => delete floor[key]); Object.assign(floor, fresh, preserved); return floor; }
  const edgeKey = (a, b) => Number(a) < Number(b) ? `${a}:${b}` : `${b}:${a}`;
  function graphText(floors) { const g = graph(floors); if (!g.explicit) return floors.slice(0, -1).map((f, i) => `${f.id}: ${floors[i + 1].id}`).join("\n"); const lines = []; for (const [id, edges] of g.nodes) { const targets = [...edges].filter((to) => Number(id) < Number(to)); if (targets.length) lines.push(`${id}: ${targets.join(", ")}`); } return lines.join("\n"); }
  function parseGraphText(source, floors) { const ids = new Set(floors.filter(object).map((f) => f.id)), edges = [], seen = new Set(), degree = new Map([...ids].map((id) => [id, 0])), issues = []; String(source).split(/\r?\n/).forEach((raw, index) => { const line = raw.trim(); if (!line) return; const m = line.match(/^(\d+)\s*:\s*(.*)$/); if (!m) return issues.push({ line: index + 1, message: "Use Floor ID: target, target" }); const from = Number(m[1]); if (!ids.has(from)) issues.push({ line: index + 1, message: `Unknown Floor ${from}` }); const targets = m[2].trim() ? m[2].split(",").map((v) => v.trim()) : []; targets.forEach((token) => { if (!/^\d+$/.test(token)) return issues.push({ line: index + 1, message: `Invalid target ${token}` }); const to = Number(token), key = edgeKey(from, to); if (!ids.has(to)) return issues.push({ line: index + 1, message: `Unknown Floor ${to}` }); if (to === from) return issues.push({ line: index + 1, message: "A Floor cannot connect to itself" }); if (seen.has(key)) return issues.push({ line: index + 1, message: `Duplicate connection ${from}–${to}` }); seen.add(key); edges.push([from, to]); degree.set(from, (degree.get(from) || 0) + 1); degree.set(to, (degree.get(to) || 0) + 1); }); }); for (const [id, count] of degree) if (count > limits.connections) issues.push({ line: 0, message: `Floor ${id} has ${count}; device limit is ${limits.connections} connections` }); return { edges, issues }; }
  function setGraphEdges(floors, edges) { const outgoing = new Map(floors.filter(object).map((f) => [f.id, []])); edges.forEach(([a, b]) => outgoing.get(Number(a))?.push(Number(b))); floors.forEach((f) => { if (object(f)) f.next = (outgoing.get(f.id) || []).sort((a, b) => a - b); }); }
  // ArduinoJson 6 on ESP32: 16-byte VariantSlot per member/array element;
  // read-only/stream input stores deduplicated UTF-8 keys and string values.
  function memoryUsage(doc) {
    let slots = 0,
      depth = 0;
    const strings = new Set();
    function visit(v, d) {
      if (typeof v === "string") strings.add(v);
      else if (Array.isArray(v)) {
        depth = Math.max(depth, d + 1);
        slots += v.length;
        v.forEach((e) => visit(e, d + 1));
      } else if (object(v)) {
        depth = Math.max(depth, d + 1);
        slots += Object.keys(v).length;
        Object.entries(v).forEach(([k, e]) => {
          strings.add(k);
          visit(e, d + 1);
        });
      }
    }
    visit(doc, 0);
    return {
      bytes: slots * 16 + [...strings].reduce((n, s) => n + bytes(s) + 1, 0),
      depth,
    };
  }
  function validate(doc, kind, catalogs = {}) {
    const issues = [];
    const issue = (path, message, severity = "error") =>
      issues.push({ path, message, severity });
    const keys = (o, allowed, p) => {
      if (!object(o)) {
        issue(p, "Expected object");
        return false;
      }
      for (const k of Object.keys(o))
        if (!allowed.includes(k))
          issue(`${p}.${k}`, "Unsupported field (retained; remove explicitly)");
      return true;
    };
    const text = (o, k, max, p, required = true, nonempty = false) => {
      if (!has(o, k) && !required) return;
      const v = o[k];
      if (typeof v !== "string" || (nonempty && !v.length))
        issue(
          `${p}.${k}`,
          "Expected " + (nonempty ? "non-empty " : "") + "string",
        );
      else if (bytes(v) > max || v.includes("\0"))
        issue(`${p}.${k}`, `Maximum ${max} UTF-8 bytes; NUL is not allowed`);
    };
    const num = (o, k, min, max, p, required = true) => {
      if (!has(o, k) && !required) return;
      const v = o[k];
      if (!Number.isInteger(v) || v < min || v > max)
        issue(`${p}.${k}`, `Expected integer ${min}…${max}`);
    };
    const choice = (o, k, values, p, required = true, nullable = false) => {
      if ((!has(o, k) && !required) || (nullable && o[k] === null)) return;
      if (!values.includes(o[k]))
        issue(`${p}.${k}`, `Expected ${values.join(" | ")}`);
    };
    const bool = (o, k, p, required = true, nullable = false) => {
      if ((!has(o, k) && !required) || (nullable && o[k] === null)) return;
      if (typeof o[k] !== "boolean") issue(`${p}.${k}`, "Expected boolean");
    };
    const array = (o, k, max, p, min = 0) => {
      const a = o[k];
      if (!Array.isArray(a)) {
        issue(`${p}.${k}`, "Expected array");
        return [];
      }
      if (a.length < min || a.length > max)
        issue(`${p}.${k}`, `Expected ${min}…${max} entries`);
      return a;
    };
    function definitions(entries, k, p, local) {
      const seen = new Set();
      entries.forEach((e, i) => {
        const q = `${p}[${i}]`;
        if (!keys(e, fields[k], q)) return;
        text(e, "id", 31, q, true, true);
        if (typeof e.id === "string" && !/^[a-z0-9_-]+$/.test(e.id))
          issue(q + ".id", "Use lowercase a-z, 0-9, underscore or hyphen");
        if (seen.has(e.id)) issue(q + ".id", "Duplicate definition ID");
        seen.add(e.id);
        if (D.builtins[k].some((b) => b.id === e.id))
          issue(q + ".id", "Reserved builtin ID");
        if (local && list(catalogs[k], k).some((b) => b?.id === e.id))
          issue(q + ".id", "Conflicts with global catalog ID");
        if (local && catalogs[k] == null)
          issue(
            q + ".id",
            "Global catalog not loaded: ID conflict check incomplete",
            "warning",
          );
        text(
          e,
          k === "enemies" ? "handle" : "name",
          k === "enemies" ? 15 : 31,
          q,
          true,
          k !== "black_ice",
        );
        if (k === "black_ice") {
          for (const stat of ["per", "spd", "atk", "def"])
            num(e, stat, 1, 10, q);
          num(e, "rez", 1, 99, q);
          choice(e, "behavior", D.behaviors, q);
          choice(e, "visual", D.visuals, q);
          choice(e, "animation", D.animations, q);
          bool(e, "player_usable", q);
          for (const f of [
            "damage_dice",
            "net_action_penalty",
            "minimum_actions",
            "status_amount",
          ])
            num(e, f, 0, 255, q, false);
          const dice = e.damage_dice ?? 0;
          if (e.behavior === "destroy_installed_program") {
            if (dice !== 0) issue(q + ".damage_dice", "This behavior does not use direct damage dice");
          } else if (
            e.behavior !== "stat_penalty" && e.behavior !== "apply_fire" &&
            D.behaviors.includes(e.behavior) &&
            (!Number.isInteger(dice) || dice < 1 || dice > 6)
          )
            issue(q + ".damage_dice", "This behavior requires 1…6 damage dice");
          if (e.behavior === "action_penalty_damage") {
            num(e, "net_action_penalty", 1, 3, q);
            num(e, "minimum_actions", 1, 5, q);
          }
          if (["move_penalty_damage", "stat_penalty"].includes(e.behavior))
            num(e, "status_amount", 1, 3, q);
        } else {
          num(e, "interface", 1, 10, q);
          num(e, "actions", 1, 5, q);
          if (k === "demons") { num(e, "rez", 1, 2147483647, q); choice(e, "visual", D.demonVisuals.map((v) => v.id), q, false); choice(e, "animation", D.demonAnimations, q, false); }
          else {
            num(e, "hp", 1, 255, q);
            choice(e, "behavior", behaviors, q, false, true);
            choice(e, "ai", ["anti_personnel"], q, false, true);
            bool(e, "dormant_until_discovered", q, false, true);
            let slots = 0;
            array(e, "programs", limits.programCount, q).forEach((p, j) => {
              const program = D.programs.find((a) => a.name === p);
              if (!program) issue(`${q}.programs[${j}]`, "Unknown program");
              else slots += program.slots;
            });
            if (slots > limits.deckSlots)
              issue(
                q + ".programs",
                "Standard deck capacity is 7 slots (duplicates consume slots too)",
              );
          }
        }
      });
    }
    function reference(id, k, p) {
      // The builtin zer=0 is a legal reference, but not a legal custom definition ID.
      if (
        typeof id !== "string" ||
        !id.length ||
        bytes(id) > 31 ||
        (!/^[a-z0-9_-]+$/.test(id) && !D.builtins[k].some((b) => b.id === id))
      ) {
        issue(p, "Invalid stable reference ID");
        return;
      }
      const found = resolve(k, id, doc, catalogs);
      if (!found)
        issue(
          p,
          catalogs[k] == null
            ? "Unresolved: catalog not loaded; preview unavailable"
            : "Unknown ID in loaded catalog / locals / builtins",
          catalogs[k] == null ? "warning" : "error",
        );
      else if (
        found.scope === "catalog" &&
        validate(catalogs[k], k).some((i) => i.severity === "error")
      )
        issue(p, "Referenced catalog is invalid; fix its errors first");
    }
    if (!kinds.includes(kind)) {
      issue("$", "Unknown document format");
      return issues;
    }
    if (
      !keys(
        doc,
        kind === "scenario" ? fields.scenario : ["format", "version", kind],
        "$",
      )
    )
      return issues;
    choice(doc, "format", [formats[kind]], "$");
    num(doc, kind === "scenario" ? "schemaVersion" : "version", 1, 1, "$");
    if (kind !== "scenario") {
      definitions(
        array(doc, kind, Number.MAX_SAFE_INTEGER, "$"),
        kind,
        `$.${kind}`,
        false,
      );
    if (list(doc, kind).length > limits.catalog)
      issue(
        `$.${kind}`,
        `Workspace contains ${list(doc, kind).length} definitions; device capacity is ${limits.catalog}. Editing is allowed, but export is blocked.`,
      );
    } else {
      text(doc, "id", 31, "$", true, true);
      text(doc, "name", 39, "$", true, true);
      text(doc, "description", 79, "$", false);
      if (typeof doc.id === "string" && !/^[a-z0-9_-]+$/.test(doc.id))
        issue(
          "$.id",
          "Non-portable filename: export uses a sanitized filename; JSON ID is preserved",
          "warning",
        );
      for (const k of kinds.slice(1))
        if (has(doc, k) && doc[k] !== null)
          definitions(
            array(doc, k, k === "enemies" ? 2 : 8, "$"),
            k,
            `$.${k}`,
            true,
          );
      if (has(doc, "demon")) reference(doc.demon, "demons", "$.demon");
      const floors = array(doc, "floors", limits.floors, "$", 1),
        ids = new Set(),
        spawns = new Set();
      const explicit = floors.some((f) => object(f) && has(f, "next"));
      floors.forEach((f, i) => {
        const p = `$.floors[${i}]`;
        if (!keys(f, fields.floor, p)) return;
        num(f, "id", 0, 255, p);
        choice(f, "type", floorTypes, p);
        if (ids.has(f.id)) issue(p + ".id", "Duplicate floor ID");
        ids.add(f.id);
        text(f, "content_id", 31, p, false);
        text(f, "description", 79, p, false);
        num(
          f,
          "dv",
          0,
          255,
          p,
          ["password", "file", "control"].includes(f.type),
        );
        choice(
          f,
          "security",
          ["low", "medium", "high"],
          p,
          f.type === "password",
        );
        for (const [key, owner] of [
          ["security", "password"],
          ["file", "file"],
          ["control", "control"],
        ]) {
          if (has(f, key) && f.type !== owner)
            issue(
              `${p}.${key}`,
              `Retained data: firmware ignores ${key} on a ${display(f.type)} floor`,
              "warning",
            );
        }
        if (f.type === "file" || has(f, "file"))
          if (keys(f.file, ["name", "type", "value"], p + ".file")) {
            text(f.file, "name", 39, p + ".file");
            text(f.file, "type", 23, p + ".file");
            num(f.file, "value", 0, 4294967295, p + ".file");
          }
        if (f.type === "control" || has(f, "control"))
          if (keys(f.control, ["name", "description"], p + ".control")) {
            text(f.control, "name", 39, p + ".control", true, true);
            text(f.control, "description", 79, p + ".control", false);
          }
        if (has(f, "ice") || f.type === "black_ice")
          array(f, "ice", 3, p, 1).forEach((id, j) =>
            reference(id, "black_ice", `${p}.ice[${j}]`),
          );
        if (has(f, "enemy")) {
          reference(f.enemy, "enemies", p + ".enemy");
          if (spawns.has(f.enemy)) issue(p + ".enemy", "Duplicate enemy spawn");
          spawns.add(f.enemy);
        }
        if (explicit) {
          const next = array(f, "next", 4, p);
          const seen = new Set();
          next.forEach((id, j) => {
            const q = `${p}.next[${j}]`;
            if (
              !Number.isInteger(id) ||
              id < 0 ||
              id > 255 ||
              !floors.some((t) => t?.id === id)
            )
              issue(q, "Unknown / invalid connection");
            if (id === f.id) issue(q, "Self connection");
            if (seen.has(id)) issue(q, "Duplicate connection");
            seen.add(id);
          });
        }
      });
      if (floors.filter((f) => object(f) && has(f, "enemy")).length > 2)
        issue(
          "$.floors",
          "Runtime supports only 2 enemy placements; additional spawns would be dropped",
        );
      const g = graph(floors);
      for (const [id, edges] of g.nodes)
        if (edges.size > 4)
          issue(
            "$.floors",
            `Floor ${display(id)}: normalized undirected degree exceeds 4`,
          );
      if (explicit && !g.explicit && floors.length > 1)
        issue(
          "$.floors",
          "All next lists are empty: firmware builds a linear chain, not disconnected floors",
          "warning",
        );
      const reached = new Set(),
        queue = [floors[0]?.id];
      while (queue.length) {
        const id = queue.pop();
        if (reached.has(id)) continue;
        reached.add(id);
        for (const next of g.nodes.get(id) || []) queue.push(next);
      }
      if (reached.size < g.nodes.size)
        issue(
          "$.floors",
          "Some floors are unreachable from the first floor (firmware accepts this graph)",
          "warning",
        );
      const count = floors.reduce(
        (n, f) =>
          n +
          (f?.type === "black_ice" && Array.isArray(f.ice) ? f.ice.length : 0),
        0,
      );
      if (count > 8)
        issue(
          "$.floors",
          "More than 8 ICE overall: firmware has 8 concurrent active ICE slots, not a total-placement limit",
          "warning",
        );
    }
    const memory = memoryUsage(doc);
    if (memory.bytes > limits.json[kind])
      issue(
        "$",
        `ArduinoJson pool estimate ${memory.bytes} exceeds ${limits.json[kind]} bytes`,
      );
    if (memory.depth > 10) issue("$", "ArduinoJson nesting limit is 10");
    return issues;
  }
  function identify(doc) {
    return kinds.find((k) => doc?.format === formats[k]);
  }
  function parse(source) {
    const doc = JSON.parse(source); // Syntax check first; never evaluate imported code.
    // JSON.parse would silently discard duplicate keys and normalize 1.0 / 1e0
    // into integers that ArduinoJson's is<int>() rejects. Reject ambiguous input.
    const tokens =
      source.match(
        /"(?:[^"\\]|\\.)*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|true|false|null|[{}\[\]:,]/g,
      ) || [];
    let pos = 0;
    function walk(path) {
      const token = tokens[pos++];
      if (token === "{") {
        const keys = new Set();
        while (tokens[pos] !== "}") {
          const key = JSON.parse(tokens[pos++]);
          if (keys.has(key)) throw Error(`${path}.${key}: duplicate JSON key`);
          keys.add(key);
          pos++;
          walk(path + "." + key);
          if (tokens[pos] !== ",") break;
          pos++;
        }
        pos++;
      } else if (token === "[") {
        let i = 0;
        while (tokens[pos] !== "]") {
          walk(`${path}[${i++}]`);
          if (tokens[pos] !== ",") break;
          pos++;
        }
        pos++;
      } else if (/^-?\d/.test(token) && /[.eE]/.test(token))
        throw Error(
          `${path}: use integer JSON notation; fractional/exponent numbers are not a public field type`,
        );
    }
    walk("$");
    const kind = identify(doc);
    if (!kind)
      throw Error("Unknown format; existing work has not been replaced");
    return { doc, kind };
  }
  function stable(v) {
    if (Array.isArray(v)) return v.map(stable);
    if (object(v))
      return Object.fromEntries(
        Object.keys(v)
          .sort()
          .map((k) => [k, stable(v[k])]),
      );
    return v;
  }
  function serialize(doc, kind, catalogs = {}) {
    const errors = validate(doc, kind, catalogs).filter(
      (i) => i.severity === "error",
    );
    if (errors.length)
      throw Error(errors.map((i) => `${i.path}: ${i.message}`).join("\n"));
    return JSON.stringify(stable(doc), null, 2) + "\n";
  }
  function destination(doc, kind) {
    return kind === "scenario"
      ? `/scenarios/${(typeof doc?.id === "string" && doc.id ? doc.id : "scenario").replace(/[^a-zA-Z0-9_-]/g, "_")}.json`
      : `/catalog/${kind}.json`;
  }
  // Imported objects may have a non-callable own "toString" property.
  const display = (value) =>
    typeof value === "object" ? JSON.stringify(value) : String(value);
  const api = {
    D,
    kinds,
    formats,
    limits,
    fields,
    floorTypes,
    neutralTypes,
    behaviors,
    has,
    object,
    clone,
    bytes,
    entry,
    blank,
    starters,
    list,
    resolve,
    graph,
    floorForType,
    setFloorType,
    graphText,
    parseGraphText,
    setGraphEdges,
    memoryUsage,
    validate,
    identify,
    parse,
    stable,
    serialize,
    destination,
    display,
  };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.NRModel = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
