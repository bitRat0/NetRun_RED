const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const M = require("../model.js"),
  P = require("../preview.js"),
  D = require("../firmware-data.js");
const { output } = require("./firmware-data.cjs");
const root = path.resolve(__dirname, "../../..");
const empty = () =>
  Object.fromEntries(M.kinds.slice(1).map((k) => [k, M.blank(k)]));
const errors = (d, k, c = empty()) =>
  M.validate(d, k, c).filter((i) => i.severity === "error");
const valid = (d, k, c = empty()) => assert.deepEqual(errors(d, k, c), []);
const invalid = (d, k, p, c = empty()) =>
  assert.ok(
    errors(d, k, c).some((i) => i.path.includes(p)),
    JSON.stringify(errors(d, k, c)),
  );
function catalog(k) {
  const d = M.blank(k);
  d[k] = [M.entry(k)];
  return d;
}
function edit(k, fn) {
  const d = k === "scenario" ? M.blank(k) : catalog(k);
  fn(k === "scenario" ? d : d[k][0], d);
  return d;
}
test("derived firmware data has no drift (source hashes, public IDs, active sprite commands)", () => {
  assert.equal(
    fs
      .readFileSync(path.join(__dirname, "../firmware-data.js"), "utf8")
      .replace(/\r\n/g, "\n"),
    output(),
  );
});
for (const k of M.kinds)
  test(`${k}: starters and complete import/export roundtrip`, () => {
    const s = M.starters(),
      json = M.serialize(s[k], k, s),
      r = M.parse(json);
    valid(r.doc, k, s);
    assert.deepEqual(r.doc, s[k]);
    assert.equal(M.serialize(r.doc, k, s), json);
    assert.equal(r.kind, k);
    assert.ok(json.endsWith("\n"));
  });
test("four on-disk starter fixtures exactly match generated models", () => {
  const s = M.starters();
  for (const k of M.kinds) {
    const target = "examples" + M.destination(s[k], k);
    assert.equal(
      fs
        .readFileSync(path.join(__dirname, "..", target), "utf8")
        .replace(/\r\n/g, "\n"),
      M.serialize(s[k], k, s),
    );
  }
});
test("current repository catalog/scenario fixtures: positive and deliberate negative cases", () => {
  const c = empty();
  for (const k of M.kinds.slice(1)) {
    c[k] = JSON.parse(
      fs.readFileSync(path.join(root, "examples/catalog", k + ".json"), "utf8"),
    );
    valid(c[k], k);
  }
  for (const file of fs
    .readdirSync(path.join(root, "examples/scenarios"))
    .filter((p) => p.endsWith(".json"))) {
    const d = JSON.parse(
      fs.readFileSync(path.join(root, "examples/scenarios", file), "utf8"),
    );
    if (/invalid|future|missing|legacy/.test(file))
      assert.ok(errors(d, "scenario", c).length, file);
    else valid(d, "scenario", c);
  }
});
test("firmware boot-test fixtures reused read-only: graph + schema + enemy behavior", () => {
  const source = fs.readFileSync(path.join(root, "src/app/App.cpp"), "utf8");
  function fixtures(start, end) {
    const area = source.slice(
      source.indexOf(start),
      source.indexOf(end, source.indexOf(start) + start.length),
    );
    const found = {};
    for (const match of area.matchAll(
      /(?:const char\*|const char)\s+(\w+)(?:\[\])?\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);/g,
    )) {
      const json = [...match[2].matchAll(/"(?:[^"\\]|\\.)*"/g)]
        .map((m) => JSON.parse(m[0]))
        .join("");
      try {
        found[match[1]] = JSON.parse(json);
      } catch {}
    }
    return found;
  }
  const g = fixtures(
    "bool branchingJsonValidationTest()",
    "void logBranchingBoundary",
  );
  assert.equal(Object.keys(g).length, 4);
  valid(g.validJson, "scenario");
  for (const key of ["unknownJson", "duplicateJson", "selfJson"])
    assert.ok(errors(g[key], "scenario").length, key);
  const b = fixtures(
    "void App::runScenarioEnemyBehaviorDebugTest()",
    "void App::runR34AuditDebugTest()",
  );
  for (const key of [
    "defaultJson",
    "baselineJson",
    "defensiveJson",
    "sentryJson",
    "builtinJson",
    "v1Json",
    "v2Json",
  ])
    valid(b[key], "scenario");
  for (const key of ["unknownJson", "wrongTypeJson", "futureJson"])
    assert.ok(errors(b[key], "scenario").length, key);
});
for (const k of M.kinds)
  test(`${k}: envelope, unknown fields, types`, () => {
    const d = k === "scenario" ? M.blank(k) : catalog(k);
    d[k === "scenario" ? "schemaVersion" : "version"] = 2;
    invalid(d, k, k === "scenario" ? "schemaVersion" : "version");
    d[k === "scenario" ? "schemaVersion" : "version"] = "1";
    invalid(d, k, k === "scenario" ? "schemaVersion" : "version");
    d.extra = "retained";
    invalid(d, k, "extra");
    assert.throws(() => M.serialize(d, k));
    assert.equal(d.extra, "retained");
    assert.ok(errors(null, k).length);
    assert.ok(errors([], k).length);
  });
test("strict JSON import preserves existing work by rejecting syntax, duplicate keys and numeric normalization", () => {
  assert.throws(() => M.parse("{"));
  assert.throws(() => M.parse("{}"));
  const s = JSON.stringify(M.blank("scenario"));
  assert.throws(
    () => M.parse(s.replace('"schemaVersion":1', '"schemaVersion":1.0')),
    /integer JSON notation/,
  );
  assert.throws(
    () => M.parse(s.replace('"schemaVersion":1', '"schemaVersion":1e0')),
    /integer JSON notation/,
  );
  assert.throws(
    () =>
      M.parse(
        s.replace('"schemaVersion":1', '"schemaVersion":1,"schemaVersion":1'),
      ),
    /duplicate/,
  );
});
test("all five floor types and all public optional floor fields roundtrip", () => {
  const d = M.blank("scenario");
  d.description = "x";
  d.demon = "demon_01";
  d.floors = [
    { id: 0, type: "password", dv: 0, security: "high", next: [1] },
    {
      id: 1,
      type: "file",
      dv: 255,
      file: { name: "", type: "", value: 4294967295 },
      next: [2],
    },
    {
      id: 2,
      type: "control",
      dv: 6,
      control: { name: "N", description: "detail" },
      next: [3],
    },
    {
      id: 3,
      type: "black_ice",
      dv: 7,
      ice: ["ice_01", "ice_01", "ice_12"],
      next: [4],
    },
    {
      id: 255,
      type: "empty",
      content_id: "meta",
      description: "detail",
      enemy: "zer=0",
      next: [],
    },
  ];
  d.floors[3].next = [255];
  valid(d, "scenario");
  assert.deepEqual(JSON.parse(M.serialize(d, "scenario", empty())), d);
});
test("numeric/string boundaries and UTF-8 (not UTF-16 length)", () => {
  for (const k of ["black_ice", "demons", "enemies"]) {
    const tests =
      k === "black_ice"
        ? {
            per: [1, 10],
            spd: [1, 10],
            atk: [1, 10],
            def: [1, 10],
            rez: [1, 99],
          }
        : k === "demons"
          ? { interface: [1, 10], rez: [1, 2147483647], actions: [1, 5] }
          : { interface: [1, 10], hp: [1, 255], actions: [1, 5] };
    for (const [key, [min, max]] of Object.entries(tests)) {
      for (const v of [min, max])
        valid(
          edit(k, (e) => (e[key] = v)),
          k,
        );
      for (const v of [min - 1, max + 1, 1.5, "2", null, true])
        invalid(
          edit(k, (e) => (e[key] = v)),
          k,
          key,
        );
    }
    const name = k === "enemies" ? "handle" : "name",
      max = k === "enemies" ? 15 : 31;
    valid(
      edit(k, (e) => (e[name] = "a".repeat(max))),
      k,
    );
    invalid(
      edit(k, (e) => (e[name] = "a".repeat(max + 1))),
      k,
      name,
    );
    invalid(
      edit(k, (e) => (e[name] = "é".repeat(Math.ceil(max / 2)))),
      k,
      name,
    );
    invalid(
      edit(k, (e) => (e[name] = "a\0b")),
      k,
      name,
    );
    for (const id of ["", "Bad.ID", "bad space", "a".repeat(32)])
      invalid(
        edit(k, (e) => (e.id = id)),
        k,
        "id",
      );
  }
  valid(
    edit("scenario", (e) => (e.name = "a".repeat(39))),
    "scenario",
  );
  invalid(
    edit("scenario", (e) => (e.name = "a".repeat(40))),
    "scenario",
    "name",
  );
  for (const dv of [-1, 256, "6", 1.5])
    invalid(
      edit("scenario", (e) => (e.floors[0].dv = dv)),
      "scenario",
      "dv",
    );
});
for (const visual of D.visuals)
  test(`visual ${visual}: accepted, distinct renderer and stable canonical export`, () => {
    const d = edit("black_ice", (e) => (e.visual = visual));
    valid(d, "black_ice");
    assert.equal(
      M.parse(M.serialize(d, "black_ice")).doc.black_ice[0].visual,
      visual,
    );
    assert.ok(D.sprites[visual].commands.length >= 7);
    assert.ok(D.sprites[visual].firmware);
  });
for (const animation of D.animations)
  test(`animation ${animation}: all 8 deterministic frames`, () => {
    valid(
      edit("black_ice", (e) => (e.animation = animation)),
      "black_ice",
    );
    const frames = [];
    for (let f = 0; f < 8; f++) {
      const c = P.attackCommands(animation, f);
      assert.ok(c.length);
      assert.deepEqual(c, P.attackCommands(animation, f));
      frames.push(JSON.stringify(c));
    }
    assert.equal(new Set(frames).size, 8);
  });
test("invalid visual, animation, behavior and unsupported presentation never export", () => {
  for (const key of ["visual", "animation", "behavior"])
    invalid(
      edit("black_ice", (e) => (e[key] = "invented")),
      "black_ice",
      key,
    );
  for (const k of ["demons", "enemies"])
    invalid(
      edit(k, (e) => (e.visual = "hound")),
      k,
      "visual",
    );
  for (const key of ["ui", "preview", "label", "renderer", "sprite"]) {
    const d = edit("black_ice", (e) => (e[key] = { frame: 3 }));
    invalid(d, "black_ice", key);
    assert.throws(() => M.serialize(d, "black_ice"));
  }
  const before = JSON.stringify(M.starters());
  P.frameState("attack", 90, "lunge");
  assert.equal(JSON.stringify(M.starters()), before);
});
for (const behavior of D.behaviors)
  test(`Black ICE effect ${behavior}: required parameters`, () => {
    const d = edit("black_ice", (e) => {
      e.behavior = behavior;
      e.damage_dice = [
        "destroy_installed_program",
        "stat_penalty",
      ].includes(behavior)
        ? 0
        : 2;
      e.net_action_penalty = 1;
      e.minimum_actions = 2;
      e.status_amount = 1;
    });
    valid(d, "black_ice");
    const e = d.black_ice[0];
    if (behavior === "destroy_installed_program") {
      e.damage_dice = 1;
      invalid(d, "black_ice", "damage_dice");
    } else if (behavior === "stat_penalty") {
      e.status_amount = 0;
      invalid(d, "black_ice", "status_amount");
    } else if (behavior !== "apply_fire") {
      e.damage_dice = 0;
      invalid(d, "black_ice", "damage_dice");
    }
  });
for (const behavior of [...M.behaviors, undefined, null])
  test(`Enemy behavior ${String(behavior)}: roundtrip without injecting defaults`, () => {
    const d = edit("enemies", (e) => {
      if (behavior === undefined) delete e.behavior;
      else e.behavior = behavior;
    });
    valid(d, "enemies");
    assert.deepEqual(M.parse(M.serialize(d, "enemies")).doc, d);
  });
test("enemy optional fields, unknown behavior/AI and deck slot limits", () => {
  valid(
    edit("enemies", (e) => {
      e.ai = "anti_personnel";
      e.dormant_until_discovered = true;
      e.programs = Array(7).fill("Armor");
    }),
    "enemies",
  );
  valid(
    edit("enemies", (e) => {
      e.ai = null;
      e.behavior = null;
      e.dormant_until_discovered = null;
      e.programs = [];
    }),
    "enemies",
  );
  for (const b of ["Baseline", "berserker", true, 0])
    invalid(
      edit("enemies", (e) => (e.behavior = b)),
      "enemies",
      "behavior",
    );
  invalid(
    edit("enemies", (e) => (e.ai = "sentry")),
    "enemies",
    "ai",
  );
  invalid(
    edit("enemies", (e) => (e.dormant_until_discovered = "false")),
    "enemies",
    "dormant",
  );
  invalid(
    edit("enemies", (e) => (e.programs = Array(8).fill("Armor"))),
    "enemies",
    "programs",
  );
  invalid(
    edit("enemies", (e) => (e.programs = ["unknown"])),
    "enemies",
    "programs",
  );
});
test("catalog capacities, reserved IDs, duplicate IDs and local conflicts for all entity kinds", () => {
  for (const k of M.kinds.slice(1)) {
    const d = M.blank(k);
    d[k] = Array.from({ length: 8 }, (_, i) => M.entry(k, "custom_" + i));
    valid(d, k);
    d[k].push(M.entry(k, "extra"));
    invalid(d, k, k);
    invalid(
      edit(k, (e) => (e.id = D.builtins[k][0].id)),
      k,
      "id",
    );
    const dup = catalog(k);
    dup[k].push(M.clone(dup[k][0]));
    invalid(dup, k, "id");
    const s = M.blank("scenario");
    s[k] = [M.entry(k)];
    const c = empty();
    c[k] = catalog(k);
    invalid(s, "scenario", "id", c);
    valid(s, "scenario", empty());
    s[k] = Array.from({ length: k === "enemies" ? 3 : 9 }, (_, i) =>
      M.entry(k, "local_" + i),
    );
    invalid(s, "scenario", k);
  }
});
test("reference resolution, unknown-vs-unavailable, invalid catalog dependencies, builtin special IDs", () => {
  const s = M.starters();
  valid(s.scenario, "scenario", s);
  const before = M.clone(s.scenario);
  const warnings = M.validate(s.scenario, "scenario");
  assert.equal(warnings.filter((i) => i.severity === "error").length, 0);
  assert.equal(warnings.length, 3);
  invalid(s.scenario, "scenario", "ice", empty());
  s.black_ice.black_ice[0].per = 999;
  invalid(s.scenario, "scenario", "ice", s);
  assert.equal(
    M.resolve("black_ice", "signal_ice", s.scenario, s).scope,
    "catalog",
  );
  s.scenario.black_ice = [M.entry("black_ice")];
  assert.equal(
    M.resolve("black_ice", "signal_ice", s.scenario, s).scope,
    "local",
  );
  assert.equal(
    M.resolve("black_ice", "ice_01", s.scenario, s).scope,
    "builtin",
  );
  assert.equal(M.resolve("enemies", "zer=0", s.scenario, s).scope, "builtin");
  assert.equal(before.black_ice, undefined);
  assert.ok(!M.serialize(before, "scenario").includes('"visual"'));
});
test("floor capacity, duplicate IDs, self/unknown/duplicate links and mixed next presence", () => {
  const d = M.blank("scenario");
  d.floors = Array.from({ length: 16 }, (_, id) => ({ id, type: "empty" }));
  valid(d, "scenario");
  d.floors.push({ id: 16, type: "empty" });
  invalid(d, "scenario", "floors");
  d.floors = [
    { id: 0, type: "empty", next: [1] },
    { id: 1, type: "empty", next: [] },
  ];
  valid(d, "scenario");
  for (const links of [[0], [99], [1, 1], [1, 1, 1, 1, 1], ["1"]]) {
    d.floors[0].next = links;
    invalid(d, "scenario", "next");
  }
  d.floors[0].next = [1];
  delete d.floors[1].next;
  invalid(d, "scenario", "next");
  d.floors[1].id = 0;
  invalid(d, "scenario", "id");
});
test("normalized fan-in degree, cycles, branches, merge, dead ends and all-empty linear fallback", () => {
  const d = M.blank("scenario");
  d.floors = Array.from({ length: 6 }, (_, id) => ({
    id,
    type: "empty",
    next: id ? [0] : [],
  }));
  invalid(d, "scenario", "floors");
  d.floors.pop();
  valid(d, "scenario");
  d.floors = [
    { id: 0, type: "empty", next: [1, 2] },
    { id: 1, type: "empty", next: [3] },
    { id: 2, type: "empty", next: [3] },
    { id: 3, type: "empty", next: [4] },
    { id: 4, type: "empty", next: [] },
  ];
  valid(d, "scenario");
  assert.equal(M.graph(d.floors).nodes.get(3).size, 3);
  d.floors.forEach((f) => (f.next = []));
  valid(d, "scenario");
  assert.equal(M.graph(d.floors).explicit, false);
  assert.ok(
    M.validate(d, "scenario").some((i) => i.message.includes("linear")),
  );
  d.floors[0].next = [1];
  assert.ok(
    M.validate(d, "scenario").some((i) => i.message.includes("unreachable")),
  );
});
test("ICE placement counts and enemy runtime cap/duplicate spawns", () => {
  invalid(
    edit(
      "scenario",
      (d) => (d.floors = [{ id: 0, type: "black_ice", ice: [] }]),
    ),
    "scenario",
    "ice",
  );
  invalid(
    edit(
      "scenario",
      (d) =>
        (d.floors = [
          { id: 0, type: "black_ice", ice: Array(4).fill("ice_01") },
        ]),
    ),
    "scenario",
    "ice",
  );
  const d = M.blank("scenario");
  d.floors = [
    { id: 0, type: "empty", enemy: "nullbyte" },
    { id: 1, type: "empty", enemy: "nullbyte" },
  ];
  invalid(d, "scenario", "enemy");
  d.floors[1].enemy = "zer=0";
  valid(d, "scenario");
  d.floors.push({ id: 2, type: "empty", enemy: "custom" });
  invalid(d, "scenario", "floors");
});
test("ESP32 ArduinoJson pool estimate deduplicates strings and blocks oversized documents", () => {
  assert.equal(M.memoryUsage({ x: ["same", "same"] }).bytes, 3 * 16 + 2 + 5);
  assert.equal(M.memoryUsage({ x: [{}] }).depth, 3);
  const d = M.blank("scenario");
  d.black_ice = Array.from({ length: 8 }, (_, i) =>
    M.entry("black_ice", "local_" + i),
  );
  d.demons = Array.from({ length: 8 }, (_, i) =>
    M.entry("demons", "demon_custom_" + i),
  );
  d.floors = Array.from({ length: 16 }, (_, id) => ({
    id,
    type: "file",
    dv: 6,
    description: String(id) + "x".repeat(76),
    content_id: "id_" + id,
    file: {
      name: "file_" + id + "a".repeat(30),
      type: "type_" + id + "x".repeat(12),
      value: id,
    },
    control: {
      name: "control_" + id,
      description: String(id) + "y".repeat(76),
    },
  }));
  invalid(d, "scenario", "$");
  assert.ok(errors(d, "scenario").some((i) => i.message.includes("pool")));
});
test("preview timing, lunge/slash translation, reveal stage clipping and distinct sprites", () => {
  assert.equal(
    new Set(D.visuals.map((v) => JSON.stringify(D.sprites[v].commands))).size,
    12,
  );
  assert.deepEqual(
    [0, 45, 90, 135, 180, 225, 270, 315].map(
      (t) => P.frameState("attack", t, "lunge").impulse,
    ),
    [0, 5, 10, 6, 3, 0, 0, 0],
  );
  assert.equal(P.frameState("reveal", 0).clip, 18);
  assert.equal(P.frameState("reveal", 350).clip, 226);
  assert.equal(P.frameState("reveal", 869).done, false);
  assert.equal(P.frameState("reveal", 870).done, true);
  assert.equal(P.frameState("attack", 360, "pulse").done, true);
});
test("required definition fields and conditional effect boundaries", () => {
  for (const kind of ["black_ice", "demons", "enemies"]) {
    const required =
      kind === "black_ice"
        ? [
            "id",
            "name",
            "per",
            "spd",
            "atk",
            "def",
            "rez",
            "behavior",
            "visual",
            "animation",
            "player_usable",
          ]
        : kind === "demons"
          ? ["id", "name", "interface", "rez", "actions"]
          : ["id", "handle", "interface", "hp", "actions", "programs"];
    for (const key of required)
      invalid(
        edit(kind, (e) => delete e[key]),
        kind,
        key,
      );
  }
  for (const [key, range] of [
    ["net_action_penalty", [1, 3]],
    ["minimum_actions", [1, 5]],
  ]) {
    const d = edit("black_ice", (e) => {
      e.behavior = "action_penalty_damage";
      e.net_action_penalty = 1;
      e.minimum_actions = 1;
    });
    for (const value of range) {
      d.black_ice[0][key] = value;
      valid(d, "black_ice");
    }
    for (const value of [range[0] - 1, range[1] + 1]) {
      d.black_ice[0][key] = value;
      invalid(d, "black_ice", key);
    }
  }
  for (const behavior of ["stat_penalty", "move_penalty_damage"]) {
    for (const value of [0, 4, 256, -1, "1"]) {
      invalid(
        edit("black_ice", (e) => {
          e.behavior = behavior;
          e.status_amount = value;
        }),
        "black_ice",
        "status_amount",
      );
    }
  }
});
test("malformed object-valued fields have safe diagnostics and output destinations", () => {
  const value = { toString: "not callable", nodeType: 1 };
  const d = edit("scenario", (e) => {
    e.id = value;
    e.floors[0].type = value;
    e.floors[0].control = { name: "N" };
  });
  assert.doesNotThrow(() => M.validate(d, "scenario"));
  assert.equal(M.destination(d, "scenario"), "/scenarios/scenario.json");
  assert.equal(M.display(value), JSON.stringify(value));
  invalid(d, "scenario", "id");
});
test("ignored floor type data is preserved with explicit warnings", () => {
  const d = edit("scenario", (e) => {
    e.floors[0].security = "low";
    e.floors[0].control = { name: "N" };
  });
  valid(d, "scenario");
  assert.equal(
    M.validate(d, "scenario").filter((i) => i.message.includes("ignores"))
      .length,
    2,
  );
  assert.deepEqual(M.parse(M.serialize(d, "scenario")).doc, d);
});
test("runtime deliverable has no network imports, fetch, module loading, eval or remote assets", () => {
  for (const file of [
    "index.html",
    "style.css",
    "app.js",
    "model.js",
    "preview.js",
    "firmware-data.js",
  ]) {
    const s = fs.readFileSync(path.join(__dirname, "..", file), "utf8");
    assert.ok(
      !/https?:\/\//.test(s.replace("http://www.w3.org/2000/svg", "")),
      file,
    );
    assert.ok(!/\b(fetch|XMLHttpRequest|WebSocket|eval)\s*\(/.test(s), file);
  }
  assert.match(
    fs.readFileSync(path.join(__dirname, "../index.html"), "utf8"),
    /connect-src 'none'/,
  );
});

test("authoring UX model: neutral switching preserves placements and initializes only active data", () => {
  const floor = M.floorForType(4, "file");
  floor.enemy = "zer";
  M.setFloorType(floor, "password");
  assert.deepEqual(floor, { id: 4, type: "password", dv: 6, security: "low", enemy: "zer" });
  M.setFloorType(floor, "black_ice");
  assert.deepEqual(floor.ice, []);
  assert.equal(M.entry("black_ice").player_usable, true);
});

test("authoring UX model: text graph normalizes branch and reports author errors", () => {
  const floors = [0, 1, 2, 3].map((id) => M.floorForType(id, "empty"));
  const parsed = M.parseGraphText("0: 1, 2\n1: 3\n2: 3", floors);
  assert.deepEqual(parsed.issues, []);
  M.setGraphEdges(floors, parsed.edges);
  assert.deepEqual(floors.map((f) => f.next), [[1, 2], [3], [3], []]);
  assert.match(M.graphText(floors), /0: 1, 2/);
  assert.match(M.parseGraphText("0: 0", floors).issues[0].message, /cannot connect to itself/);
  assert.match(M.parseGraphText("0: 1\n1: 0", floors).issues[0].message, /Duplicate/);
});

test("authoring UX model: workspace catalog overflow is retained but blocks device export", () => {
  const doc = M.blank("black_ice");
  doc.black_ice = Array.from({ length: 9 }, (_, i) => M.entry("black_ice", `ice_${i}`));
  assert.equal(doc.black_ice.length, 9);
  assert.ok(errors(doc, "black_ice").some((i) => /Workspace contains 9/.test(i.message)));
  assert.throws(() => M.serialize(doc, "black_ice"), /device capacity/);
});
test("mixed Schema 1 Floors retain neutral content, Black ICE and Enemy Netrunner", () => {
  const doc = M.blank("scenario");
  doc.floors = [{ id: 0, type: "file", dv: 6, file: { name: "MIX.LOG", type: "LOG", value: 1 }, ice: ["ice_01"], enemy: "nullbyte" }];
  valid(doc, "scenario");
  assert.deepEqual(M.parse(M.serialize(doc, "scenario")).doc.floors[0].ice, ["ice_01"]);
  doc.floors[0].ice = ["ice_01", "ice_02", "ice_03", "ice_04"];
  invalid(doc, "scenario", ".ice");
});

test("Black ICE apply_fire accepts configured direct damage", () => {
  const d = edit("black_ice", (e) => { e.behavior = "apply_fire"; e.damage_dice = 2; });
  valid(d, "black_ice");
  const roundtrip = M.parse(M.serialize(d, "black_ice")).doc.black_ice[0];
  assert.equal(roundtrip.behavior, "apply_fire");
  assert.equal(roundtrip.damage_dice, 2);
});

test("neutral Floor type changes preserve all independent hostile placement state", () => {
  const floor = M.floorForType(0, "password");
  floor.dv = 15;
  floor.security = "low";
  floor.ice = ["ice_01"];
  floor.enemy = "nullbyte";
  floor.next = [2, 3];
  const snapshot = { ice: [...floor.ice], enemy: floor.enemy, next: [...floor.next] };
  for (const type of ["control", "file", "empty", "password"]) {
    M.setFloorType(floor, type);
    assert.deepEqual(floor.ice, snapshot.ice);
    assert.equal(floor.enemy, snapshot.enemy);
    assert.deepEqual(floor.next, snapshot.next);
    assert.equal(floor.id, 0);
    assert.equal(floor.type, type);
  }
const scenario = M.blank("scenario");
  scenario.demon = "demon_01";
  scenario.floors = [floor, M.floorForType(2, "empty"), M.floorForType(3, "empty")];
  scenario.floors.slice(1).forEach((f) => (f.next = []));
  M.setFloorType(floor, "control");
  assert.equal(scenario.demon, "demon_01");
  valid(scenario, "scenario");
  const roundtrip = M.parse(M.serialize(scenario, "scenario")).doc;
  assert.equal(roundtrip.demon, "demon_01");
  assert.deepEqual(roundtrip.floors[0].ice, ["ice_01"]);
  assert.equal(roundtrip.floors[0].enemy, "nullbyte");
  assert.deepEqual(roundtrip.floors[0].next, [2, 3]);
});
test("built-in Demon preview identity follows firmware visual definitions", () => {
  const builtins = D.builtins.demons;
  assert.deepEqual(builtins.map((d) => [d.name, d.visual]), [
    ["LATCH", "orb"], ["WARDEN", "sentinel"], ["CROWN", "crown"],
  ]);
  assert.equal(new Set(builtins.map((d) => d.visual)).size, 3);
  const custom = M.entry("demons");
  assert.equal(custom.visual, "orb");
  custom.visual = "crown";
  valid({ format: M.formats.demons, version: 1, demons: [custom] }, "demons");
});
test("Custom Demon presentation defaults, validation and roundtrip", () => {
  const d = M.blank("demons");
  d.demons = [M.entry("demons")];
  valid(d, "demons");
  assert.equal(d.demons[0].visual, "orb");
  assert.equal(d.demons[0].animation, "pulse");
  d.demons[0].visual = "sentinel";
  d.demons[0].animation = "slash";
  valid(d, "demons");
  const restored = M.parse(M.serialize(d, "demons")).doc.demons[0];
  assert.equal(restored.visual, "sentinel");
  assert.equal(restored.animation, "slash");
  d.demons[0].visual = "foo";
  invalid(d, "demons", "visual");
  d.demons[0].visual = "orb";
  d.demons[0].animation = "bar";
  invalid(d, "demons", "animation");
  delete d.demons[0].visual; delete d.demons[0].animation;
  valid(d, "demons");
});