/* Development only: derive public IDs and drawing commands from trusted repo source.
 * No firmware execution, browser eval, dependencies, or generated firmware changes.
 * Run --check for drift; --print emits the checked-in browser data for apply_patch. */
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const crypto = require("node:crypto");
const root = path.resolve(__dirname, "../../..");
const read = (p) =>
  fs.readFileSync(path.join(root, p), "utf8").replace(/\r\n/g, "\n");
const sources = [
  "src/content/ScenarioLoader.cpp",
  "src/content/ScenarioImport.h",
  "src/content/ScenarioLoader.h",
  "src/content/ArchitectureDefinition.h",
  "src/content/ArchitectureFactory.cpp",
  "src/content/ScenarioScanner.cpp",
  "src/content/ScenarioScanner.h",
  ...["BlackIce", "Demon", "EnemyNetrunner"].flatMap((n) => [
    `src/content/${n}Registry.cpp`,
    `src/content/${n}Registry.h`,
    `src/game/${n}.cpp`,
    `src/game/${n}.h`,
  ]),
  "src/game/NetTypes.h",
  "src/game/VisualIds.h",
  "src/game/CyberdeckConfig.h",
  "src/game/CyberdeckConfig.cpp",
  "src/game/ProgramCatalog.cpp",
  "src/game/GameState.cpp",
  "src/app/App.cpp",
  "src/ui/DisplayManager.cpp",
  "src/ui/DisplayManager.h",
  "src/ui/GameUIController.cpp",
  "src/ui/GameUIController.h",
];
function body(source, name) {
  const signature = source.indexOf(`void DisplayManager::${name}(`);
  if (signature < 0) throw Error(`Missing renderer ${name}`);
  const start = source.indexOf("{", signature);
  if (start < 0) throw Error(`Missing renderer ${name}`);
  let depth = 1,
    end = start + 1;
  while (depth && end < source.length) {
    if (source[end] === "{") depth++;
    if (source[end] === "}") depth--;
    end++;
  }
  return source.slice(start + 1, end - 1);
}
function generate() {
  const registry = read("src/content/BlackIceRegistry.cpp");
  const display = read("src/ui/DisplayManager.cpp");
  const visuals = [
    ...registry.match(/names\[\] = \{([^}]+)\}/)[1].matchAll(/"([^"]+)"/g),
  ].map((m) => m[1]);
  const mappings = [
    ...body(display, "drawIceVisual").matchAll(
      /case IceVisualId::(\w+): (draw\w+)\(/g,
    ),
  ];
  const animations = [
    ...registry.matchAll(
      /strcmp\(value, "(\w+)"\)\) result = HostileAttackStyle::/g,
    ),
  ].map((m) => m[1]);
  const behaviors = [
    ...registry.matchAll(
      /strcmp\(value, "(\w+)"\)\) result = BlackIceEffectType::/g,
    ),
  ].map((m) => m[1]);
  const colors = {
    TFT_BLACK: 0,
    TFT_WHITE: 65535,
    TFT_RED: 63488,
    TFT_DARKGREY: 31727,
    TFT_LIGHTGREY: 50712,
    UI_RED: 63488,
    UI_MAGENTA: 63519,
    UI_DIM: 31727,
    UI_GREEN: 2016,
    UI_YELLOW: 65504,
  };
  function capture(name, extra = {}, activeOnly = false) {
    let code = body(display, name);
    if (activeOnly) code = code.split("return;")[0];
    code = code
      .replace(/\/\/[^\n]*/g, "")
      .replace(
        /const int8_t (\w+)\[\]\[\d+\] = (\{.*?\});/g,
        (_, name, value) =>
          `const ${name} = ${value.replace(/\{/g, "[").replace(/\}/g, "]")};`,
      )
      .replace(/\bconst (?:u?int\d+_t|int)\b/g, "const")
      .replace(/\b(?:u?int\d+_t|int|size_t)\b/g, "let")
      .replace(/static_cast<let>\(([^()]+)\)/g, "($1)")
      .replace(/const auto& (\w+) : (\w+)/g, "const $1 of $2")
      .replace(/M5Cardputer.Display\./g, "display.");
    const commands = [];
    const displayProxy = new Proxy(
      {},
      {
        get:
          (_, op) =>
          (...args) => {
            if (op === "setTextColor") return;
            if (
              !/^(fill|draw)(Triangle|RoundRect|Rect|Circle|Ellipse|Line|FastHLine|FastVLine|Pixel)$/.test(
                op,
              )
            )
              throw Error(`Unsupported ${op}`);
            commands.push([op, ...args]);
          },
      },
    );
    vm.runInNewContext(
      `(function(){${code}})()`,
      {
        ...colors,
        display: displayProxy,
        x: 0,
        y: 0,
        tone: 0,
        toneColor: () => 63488,
        spriteInnerColor: () => 30720,
        spriteHighlightColor: () => 63519,
        active: true,
        type: 0,
        ...extra,
      },
      { timeout: 1000 },
    );
    if (
      !commands.length ||
      commands.some((c) => c.slice(1).some((n) => !Number.isFinite(n)))
    )
      throw Error(`Bad commands ${name}`);
    return commands;
  }
  const sprites = Object.fromEntries(
    visuals.map((id, i) => [
      id,
      {
        label: id[0].toUpperCase() + id.slice(1),
        firmware: mappings[i][1],
        renderer: mappings[i][2],
        commands: capture(mappings[i][2], {}, true),
      },
    ]),
  );
  sprites.enemy = {
    label: "Enemy bust (fixed)",
    renderer: "drawEnemyNetrunner",
    commands: capture("drawEnemyNetrunner"),
  };
  for (let type = 0; type <= 3; type++)
    sprites[`demon${type}`] = {
      label: "Demon actor (fixed)",
      renderer: "drawDemonActor",
      commands: capture("drawDemonActor", { type }),
    };
  const ice = [
    ...read("src/game/BlackIce.cpp").matchAll(
      /\{"(ice_\d+)", "([^"]+)"[\s\S]*?IceVisualId::(\w+), HostileAttackStyle::(\w+)/g,
    ),
  ].map((m) => ({
    id: m[1],
    name: m[2],
    visual: visuals[mappings.findIndex((v) => v[1] === m[3])],
    animation: m[4].toLowerCase(),
  }));
  const demonSource = read("src/game/Demon.cpp");
  const demonDefinitions = [
    ...demonSource.matchAll(
      /\{"(demon_\d+)", "([^"]+)", DemonType::Demon\d+, DemonVisualId::(\w+)/g,
    ),
  ];
  const demonVisualNames = [
    ...demonSource.match(/demonVisualIdName[\s\S]*?names\[\] = \{([^}]+)\}/)[1].matchAll(/"([^"]+)"/g),
  ].map((m) => m[1]);
  const demonVisualLabels = [
    ...demonSource.match(/demonVisualIdLabel[\s\S]*?labels\[\] = \{([^}]+)\}/)[1].matchAll(/"([^"]+)"/g),
  ].map((m) => m[1]);
  const demonVisuals = demonVisualNames.map((id, index) => ({
    id,
    label: demonVisualLabels[index],
    sprite: `demon${index + 1}`,
  }));
  const demons = demonDefinitions.map((m) => ({
    id: m[1], name: m[2], visual: m[3].replace(/01$/, "").toLowerCase(),
  }));
  const demonAnimations = animations;
  const enemies = [
    ...read("src/game/EnemyNetrunner.cpp").matchAll(
      /"(nullbyte|zer=0)", "([^"]+)"/g,
    ),
  ].map((m) => ({ id: m[1], handle: m[2], visual: "enemy" }));
  const programs = [
    ...read("src/game/ProgramCatalog.cpp").matchAll(
      /\{ProgramId::\w+, "([^"]+)", ProgramType::(\w+), \d+, \d+, \d+, (\d+)/g,
    ),
  ].map((m) => ({ name: m[1], type: m[2], slots: Number(m[3]) }));
  return {
    sourceHead: "09060c7177290e2c46d40c2c8850f536e12ac341",
    fingerprints: Object.fromEntries(
      sources.map((p) => [
        p,
        crypto.createHash("sha256").update(read(p)).digest("hex"),
      ]),
    ),
    visuals,
    animations,
    demonVisuals,
    demonAnimations,
    behaviors,
    sprites,
    builtins: { black_ice: ice, demons, enemies },
    programs,
  };
}
function output() {
  return `/* Generated by dev/firmware-data.cjs. Do not edit. */\n(function(root){\n'use strict';\nconst data = ${JSON.stringify(generate(), null, 2)};\nif(typeof module==='object' && module.exports) module.exports=data;\nelse root.NRData=data;\n})(typeof globalThis!=='undefined'?globalThis:this);\n`;
}
if (require.main === module) {
  if (process.argv.includes("--print")) process.stdout.write(output());
  else if (read("tools/scenario-builder/firmware-data.js") !== output()) {
    console.error(
      "Firmware contract drift: review validators and previews, regenerate data.",
    );
    process.exitCode = 1;
  } else
    console.log(
      "Firmware contracts, public IDs and sprite commands: no drift.",
    );
}
module.exports = { generate, output };
