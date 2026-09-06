# NETRUN // RED — Offline Scenario Builder

Open **index.html** directly in a current desktop browser. Keep the six runtime
files together: `index.html`, `style.css`, `firmware-data.js`, `model.js`,
`preview.js`, `app.js`. No installation, server, account or connection is needed.
The complete folder can be copied elsewhere. Runtime has zero dependencies.

## Authoring

1. Choose Scenario, Black ICE, Demons or Enemy Netrunners in the left navigation.
2. Start with **New document**, import an existing JSON file, or load the four
   homebrew documents with **Load starter set** (replaces all four documents).
3. Create catalog definitions, then reference their stable IDs in the Scenario.
   Reference fields suggest local, loaded catalog and builtin IDs.
4. Add floors, choose a type and explicitly **Initialize missing type fields**.
   Changing a type never silently deletes existing data. Remove obsolete nested
   data with its remove control or the JSON repair editor.
5. Use implicit list order for a line, or enable explicit connections. The graph
   displays the actual normalized undirected edges; the first floor is entry.
   Checkboxes edit links to known floors; the connection list also lets you repair
   imported invalid IDs. Reorder buttons keep IDs and explicit references intact.
6. Choose ICE visual/animation selectors or visual thumbnails. IDLE, REVEAL,
   ATTACK and REPLAY operate only on preview state, never on exported content.
7. Resolve red validation errors. Yellow warnings require explicit confirmation
   at export. Download the active document; repeat for its referenced catalogs.
8. Copy files manually to the displayed SD destinations. A browser download does
   not write to the Cardputer or create directories on its SD card.

Edits are autosaved locally in the browser and restored when this same `file://` builder is reopened. The saved workspace is browser-local only: there is no backend, cloud storage, account or telemetry. Use **New Scenario** to replace only the active Scenario while reusable catalogs remain available. Use **Clear local workspace** to remove the saved Scenario and all catalogs. JSON downloads remain the portable backup; browser/site data can be cleared.

New/import/delete actions ask for confirmation. Import replaces only the recognized document kind. Cancel preserves the existing document. No undo stack is provided.

### Catalog availability and portable scenarios

Initially catalog contents are **unknown**, not known-empty. Importing, creating
or editing a catalog marks it loaded. **Confirm catalog loaded** explicitly treats
the displayed empty catalog as authoritative; **Mark catalog unavailable** removes
it from offline reference resolution without deleting the document.

An unknown reference is an error when its catalog is loaded. With no catalog
available, it is an incomplete-check warning and has no invented thumbnail.
A reference into an invalid catalog blocks Scenario export. Loaded catalog IDs
and local IDs must match what will actually be on the SD card.

**Advanced / LOCAL & PORTABLE definitions** explicitly enables each local
definition array. This does not copy global definitions or change their IDs.
Local entries use the same forms and visual preview as catalogs. Resolution is
local → global → builtin, but definitions may not shadow a builtin or a loaded
global ID. An unavailable global catalog means local-ID conflict checking is
incomplete. A portable file must not be accompanied by a catalog containing the
same custom IDs. One root `demon` selects one runtime demon, even if several demon
definitions exist.

### Import, JSON repair and output

Imports preserve supported values, arrays, omitted fields, null defaults and
canonical selections; no defaults are injected into existing documents. Valid
JSON with invalid fields is retained and reported with field paths. Unknown keys
are retained but block export until explicitly removed in the JSON repair editor.
Raw JSON edits must be applied or discarded before any other model edit/export.
Syntax errors and unknown formats leave existing work intact.

Duplicate JSON keys and fractional/exponent number tokens are rejected explicitly:
native `JSON.parse` would discard duplicate keys or turn `1.0`/`1e0` into integer
`1`, masking firmware type errors. Public numeric fields use integer tokens.
Unrecognized metadata is never silently stripped to manufacture a valid file.
Output is two-space JSON, deterministically sorted object keys, original array
order, final newline. Preview/UI state is separate; serialization also validates
the document, so callers cannot bypass the UI export gate.

## Source-derived public contract

Generated firmware-data snapshot inspected: branch `r3-dev`, source commit
`09060c7177290e2c46d40c2c8850f536e12ac341`. Current project baseline is
`c917982 feat: persist scenario builder workspace locally`; see `docs/NETRUN_PROJECT_STATUS.md`.
Authoritative files are `src/content/ScenarioLoader.cpp`, `ScenarioImport.h`,
the three `*Registry.{h,cpp}` pairs, `ArchitectureDefinition.h`,
`ArchitectureFactory.cpp`, `ScenarioScanner.{h,cpp}`, `src/game/NetTypes.h`,
`BlackIce.*`, `Demon.*`, `EnemyNetrunner.*`, `CyberdeckConfig.*`, `ProgramCatalog.cpp`,
`GameState.cpp`, `VisualIds.h`, and `src/ui/DisplayManager.{h,cpp}` and
`GameUIController.cpp`. Existing graph/import/custom-entity/behavior/visual boot
checks in `src/app/App.cpp` and current `examples/` were inspected.

| Document  | `format`                   | Version field      | Entry array / destination              |
| --------- | -------------------------- | ------------------ | -------------------------------------- |
| Scenario  | `netrun-architecture`      | `schemaVersion: 1` | `floors`, `/scenarios/<id>.json`       |
| Black ICE | `netrun-black-ice-catalog` | `version: 1`       | `black_ice`, `/catalog/black_ice.json` |
| Demon     | `netrun-demon-catalog`     | `version: 1`       | `demons`, `/catalog/demons.json`       |
| Enemy     | `netrun-enemy-catalog`     | `version: 1`       | `enemies`, `/catalog/enemies.json`     |

There is no V1/V2/V3 migration or schema selector. These version-field spellings
are intentionally different. Catalog arrays may be empty.

### Scenario / floors

- Required root fields: `format`, `schemaVersion`, `id`, `name`, `floors`.
  `id`: non-empty, max 31 UTF-8 bytes; `name`: non-empty, max 39 bytes.
  Optional `description`: max 79 bytes. The loader does not restrict Scenario ID
  characters; non-portable IDs get a filename warning and sanitized **filename**,
  without changing the JSON ID. Scenario ID uniqueness across other SD files
  cannot be established from the one active Scenario document.
- Optional `black_ice`, `demons`, `enemies`: local definition arrays (null is
  treated as absent by the loader). Optional `demon`: one stable reference ID.
- 1–16 floors. Each has unique integer `id` 0–255 and `type` from `empty`,
  `password`, `file`, `control`, `black_ice`.
- Optional floor `content_id` max 31 bytes and `description` max 79 bytes.
  Optional `dv` integer 0–255 on any floor; required for password/file/control.
- Password requires `security`: `low`, `medium`, `high`.
- File requires `file: {name, type, value}`: name max 39 bytes, type max 23 bytes,
  value integer 0–4294967295. Empty file name/type strings are accepted by source.
- Control requires `control: {name, description?}`: non-empty name max 39 bytes,
  optional description max 79 bytes.
- Black ICE requires `ice`: 1–3 stable-ID references. Repeated ICE IDs are legal.
  `enemy` is independent and can be placed on any floor, including an ICE floor.
  The same enemy ID cannot spawn twice. Runtime supports 2 enemy placements.
- Graph: either omit `next` on every floor or give every floor an array. Each
  array allows 0–4 distinct, non-self, existing floor IDs. Incoming and outgoing
  links normalize to undirected edges; total normalized degree must be ≤4.
  Branches, merges and cycles are supported. Disconnected graphs are accepted
  by firmware but warned about by the builder.
- All `next: []` on multiple floors produces a **linear chain** in the factory,
  not disconnected nodes. The graph and warning reflect this behavior.

### Black ICE

Required: `id`, `name`, `per`, `spd`, `atk`, `def`, `rez`, `behavior`, `visual`,
`animation`, `player_usable`. Custom IDs are 1–31 lowercase ASCII letters,
digits, `_` or `-`. Name is max 31 UTF-8 bytes (the ICE parser allows empty names).
PER/SPD/ATK/DEF are integers 1–10, REZ 1–99, player_usable is boolean.

| `behavior`                  | Required effect parameters                                         |
| --------------------------- | ------------------------------------------------------------------ |
| `direct_damage`             | `damage_dice` 1–6                                                  |
| `derezz_defender_damage`    | `damage_dice` 1–6                                                  |
| `destroy_installed_program` | `damage_dice` 0 or omitted                                         |
| `navigation_lock_damage`    | `damage_dice` 1–6                                                  |
| `action_penalty_damage`     | `damage_dice` 1–6, `net_action_penalty` 1–3, `minimum_actions` 1–5 |
| `program_damage`            | `damage_dice` 1–6                                                  |
| `apply_fire`                | `damage_dice` 0 or omitted                                         |
| `move_penalty_damage`       | `damage_dice` 1–6, `status_amount` 1–3                             |
| `stat_penalty`              | `status_amount` 1–3                                                |
| `unsafe_jack_out_damage`    | `damage_dice` 1–6                                                  |

The four effect-number fields are optional integers, defaulting to 0 when omitted,
unless their behavior requires them. Their storage is uint8; the authoring tool
rejects non-integers and values outside 0–255 rather than relying on parser
coercion/wrapping. Irrelevant effect fields are preserved, not automatically reset.

Visual IDs: `hound`, `bird`, `serpent`, `octopus`, `wraith`, `hunter`, `scorpion`,
`rat`, `winged`, `feline`, `skull`, `giant`.
Animation IDs: `lunge`, `pulse`, `slash`, `burst`. There is no public reveal-style
field; all use the shared firmware appearance sequence.

### Demon

Required fields: id, name, interface, rez, actions; optional public visual
(orb, sentinel, crown) and animation (lunge, pulse, slash, burst). ID follows
the same custom-ID rule; non-empty name is max 31 bytes. Interface is 1–10,
actions 1–5, and REZ is at least 1 within the parser signed 32-bit range.
Omitted presentation values use legacy defaults. Selected presentation is
independent of Demon gameplay type and is used for floor, reveal, and attack
previews. This tool does not add multi-Demon runtime support.

### Enemy Netrunner

Required: `id`, `handle`, `interface`, `hp`, `actions`, `programs`.
ID follows the custom-ID rule, handle is non-empty and max **15** UTF-8 bytes,
interface 1–10, HP 1–255, actions 1–5. Programs are exact, case-sensitive names
derived from `ProgramCatalog.cpp`, not custom free-form names. The fixed Standard
deck has 7 slots. Array storage permits 9 entries, but each current program costs
1 slot, so at most 7 currently fit. Empty decks and duplicates are accepted.
No hardware/deck-quality/visual/animation fields are exposed by this schema.

Optional `behavior`: `baseline`, `defensive`, `sentry`; absent or null defaults to
baseline without adding a field to exported imports. Defensive prioritizes valid
Defender activation and otherwise falls back to baseline; sentry suppresses remote
chase and uses baseline combat when co-located. Optional `ai` accepts only
`anti_personnel` (or null/default), and `dormant_until_discovered` boolean or
null/default. No new AI is implemented.

### Capacities / stricter authoring diagnostics

Each global catalog holds 8 custom definitions. A Scenario holds up to 8 local
ICE, 8 local demons and 2 local enemy definitions. Builtin IDs are reserved within
their respective kind; the builtin enemy `zer=0` is a legal **reference**, not a
legal custom ID. The builder includes builtin names/visual metadata for resolution,
not editable builtin statblock presets.

ArduinoJson memory pools: Scenario 8192, ICE 6144, Demon 4096, Enemy 6144 bytes.
These are **not file-byte limits**. The validator estimates ESP32 ArduinoJson 6
memory using 16-byte slots and deduplicated UTF-8 strings, with nesting limit 10.
Large documents exceeding the estimate block export. This is not a hardware RAM
or fragmentation guarantee. Whitespace does not consume the pool.

Authoring is intentionally stricter than permissive parser behavior: unknown
keys, wrong optional field types, NUL strings, numeric coercion/wrapping, ignored
ICE placements on non-ICE floors and >2 enemy spawns are not silently accepted.
No input values are clamped. There is no invented overall 8-ICE placement limit:
the firmware's 8 **active** ICE slots only produce a concurrency warning when
there are more placements overall.

## Visual implementation and drift protection

`dev/firmware-data.cjs` reads the repository's own accepted drawing routines,
translates the trusted active primitive subset in a development-only VM, and
captures draw commands into `firmware-data.js`. It stops at the first active ICE
`return`, excluding unreachable historical artwork. It also derives public enum
IDs, builtin identity/visual mappings and program names/slot costs. There is no
eval, generator or firmware dependency at browser runtime.

`preview.js` renders those commands with Canvas at the 240×135 stage aspect ratio,
48-pixel ICE coordinate system and RGB565 colors. Appearance is six 70 ms frames,
the final 226-pixel reveal clip, then a 450 ms hold. Attacks use eight 45 ms frames,
the actual lunge/slash/pulse/burst translations and effect coordinates, including
hit feedback. Every public ICE visual and animation is previewable. Enemy attacks
show the first deck program's existing firmware effect, not an AI decision;
Demon previews use the selected public visual and animation. Static appearance
deliberately has no invented idle motion. Raster edges can differ from the LCD's integer drawing implementation;
the browser is not a complete screen/game emulator.

Generated metadata is isolated from serialization. Hashes protect source contracts
and renderer files against drift; public ID/renderer mapping, source derivation,
timings, model validation and serialization have independent tests. If source
changes, review `model.js` and `preview.js` before regenerating the data with
`node dev/firmware-data.cjs --print`. Do not simply refresh hashes to bypass review.
Only the project's existing neutral drawing primitives are reused; no corebook
art or official ICE/Demon statblock presets were added.

## Checks (from repository root)

```text
node --test --test-reporter=spec tools/scenario-builder/dev/model.test.cjs
node tools/scenario-builder/dev/firmware-data.cjs --check
node --check tools/scenario-builder/model.js
node --check tools/scenario-builder/preview.js
node --check tools/scenario-builder/app.js
node tools/scenario-builder/dev/browser-check.cjs
```

Node 22+ is only needed for development checks, not for using the builder.
The dependency-free browser check defaults to installed Windows Edge. Set
`BROWSER_EXE` to another Chromium-family executable if needed. It uses a fresh
temporary browser profile (never the user's profile), `file://`, offline network
emulation, real CDP mouse clicks, native file inputs and real Blob downloads.
Screenshots, a 12-visual gallery and downloads remain in a reported temporary
directory for inspection. It requires a process environment that permits browser
renderer subprocesses; restricted sandbox execution can time out at `Page.enable`.

Tests cover the four models, limits, strict imports, roundtrip stability,
local/global/builtin references, malformed values, graph normalization,
source drift, all visual IDs, all animation IDs, all ICE effects, Enemy behaviors,
metadata exclusion and offline dependencies. Current repository example files and
selected JSON literals from existing firmware boot tests are validated read-only.
This reuses their **fixtures**, not a claim to have executed the ESP32 boot tests.

## Discovery conflicts / remaining verification boundaries

- `docs/SCENARIO_CATALOGS.md` describes the current envelopes and catalog split.
  Older `docs/scenarios/SCENARIO_FORMAT.md` and historical examples still contain
  removed official-name ICE references, incomplete floor/entity descriptions and
  graph/feature wording that conflicts with the current parser. They are not used
  as the builder schema; project-wide docs were intentionally left unchanged.
- Loader graph syntax checking and factory normalized-degree checking are
  separate; the builder enforces both. All-empty `next` arrays fall back to a
  line, and disconnected explicit graphs are not rejected by firmware.
- The loader does not reject >2 distinct enemy placements, but `GameState` only
  initializes 2; the builder blocks this silent runtime loss.
- Demon REZ is not capped at 99/255 by the current parser. Enemy JSON handle
  storage is 15 bytes even though another runtime display buffer is larger.
- Optional parser fields sometimes accept null/ignore wrong types or coerce
  effect numbers. The documented safe authoring checks above are explicit;
  firmware was not changed to align with the builder.
- Catalog contents on an actual SD, other Scenario IDs/files, LCD raster fidelity,
  heap availability and gameplay behavior cannot be fully established offline.
  Browser acceptance is automated Chromium/Edge plus screenshot inspection;
  Firefox/Safari and a human-operated end-to-end browser session are not certified.
- No firmware files, gameplay, parser behavior, project roadmap or platform
  configuration were modified. No upload or on-device boot tests were performed.

## SD acceptance fixtures

The four files under this tool's `examples/` form one neutral homebrew starter set.
Copy them maintaining these paths; the Scenario references the three catalogs
and intentionally contains no local definitions:

```text
/catalog/black_ice.json
/catalog/demons.json
/catalog/enemies.json
/scenarios/signal_relay.json
```

They exercise a password entry, branch, merge, file, control node, custom ICE,
an independent enemy on the same ICE floor, and one demon reference.
These are software fixtures, not hardware acceptance evidence.

HARDWARE TEST: NOT RUN
