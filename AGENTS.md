# Cardputer ADV – Codex Project Instructions

## Project

This project develops custom firmware for the **M5Stack Cardputer ADV** using:

* VS Code
* PlatformIO
* Arduino Framework
* ESP32-S3

PlatformIO currently uses:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
```

Do not change the PlatformIO board or framework unless required and explain the reason before doing so.

---

## Hardware

Target device:

**M5Stack Cardputer ADV**

Main hardware relevant to the project:

* ESP32-S3
* Integrated display
* Integrated keyboard
* Wi-Fi
* Bluetooth
* Speaker
* Microphone
* microSD
* USB-C
* GPIO expansion

Hardware-specific code should be isolated where practical so application logic does not directly depend on low-level hardware APIs.

---

## Current Project Status

The basic hardware test has been completed successfully.

Confirmed working:

* Firmware compilation with PlatformIO
* Firmware upload to Cardputer ADV
* Display initialization
* Text output on display
* Keyboard initialization
* Keyboard input

Do not remove or break these working capabilities when restructuring the project.

---

## Current Release Status

The current project release is **NETRUN // RED 1.0.0**.

Status: **Hardware Accepted / Feature Frozen**.

The older V0.1 development and scope notes below are historical context, not
an active implementation roadmap. Do not start new gameplay, UI, sprite,
content, or architecture work unless the user explicitly requests a later
version or a bug fix.

# Architecture Goal

The project should evolve from a hardware test into a modular Cardputer application framework.

Avoid putting the complete application into `main.cpp`.

Target structure:

```text
src/
├── main.cpp
│
├── app/
│   ├── App.cpp
│   └── App.h
│
├── ui/
│   ├── DisplayManager.cpp
│   └── DisplayManager.h
│
└── input/
    ├── KeyboardManager.cpp
    └── KeyboardManager.h
```

Additional modules can be introduced later when required.

Examples:

```text
network/
storage/
audio/
apps/
system/
```

Do not create unnecessary abstractions before they are needed.

---

# Responsibilities

## main.cpp

`main.cpp` should remain minimal.

Its responsibilities should ideally be limited to:

* hardware/framework startup
* creating the application
* calling application setup
* calling application update loop

Example concept:

```cpp
void setup()
{
    app.begin();
}

void loop()
{
    app.update();
}
```

---

## App

`App` controls the high-level application lifecycle.

Responsibilities may include:

* initialization
* application state
* coordination between input and UI
* future menu/application switching

Application logic should live here or in dedicated modules instead of `main.cpp`.

---

## DisplayManager

Responsible for display-related functionality.

Examples:

* display initialization
* clearing the screen
* drawing text
* status information
* menus
* future UI components

Other modules should preferably use `DisplayManager` instead of directly accessing the display hardware.

---

## KeyboardManager

Responsible for keyboard handling.

Examples:

* keyboard initialization
* keyboard polling
* detecting new key presses
* exposing typed characters to the application

Avoid spreading direct keyboard hardware access across the project.

---

# Coding Guidelines

Use modern, readable C++ appropriate for embedded development.

Prefer:

* small classes
* clear responsibilities
* descriptive names
* header/source separation
* simple interfaces
* minimal global state

Avoid:

* very large functions
* duplicated hardware initialization
* unnecessary dynamic allocation
* unnecessary dependencies
* blocking delays where they interfere with input or UI

Keep RAM and flash limitations of the ESP32-S3 in mind.

---

# Hardware Safety

Do not guess Cardputer ADV GPIO assignments.

Before introducing direct GPIO access:

1. Verify the pin assignment.
2. Prefer the appropriate M5Stack library/API when available.
3. Avoid changing pins already used by integrated Cardputer hardware.

Never introduce code that could create conflicting GPIO output states.

---

# Build Workflow

After modifying source code, Codex should run:

```bash
pio run
```

If compilation fails:

1. Read the compiler error.
2. Identify the actual cause.
3. Fix the relevant code.
4. Run `pio run` again.

Continue until the project builds successfully or a hardware/user decision is required.

Do not hide compiler errors.

---

# Upload

Do not automatically upload firmware unless explicitly requested.

When requested, use:

```bash
pio run --target upload
```

Compilation should succeed before upload.

---

# Refactoring Rule

Working functionality should be preserved during refactoring.

In particular, the currently working:

* display output
* keyboard input

must continue working after architectural changes.

Prefer incremental changes over rewriting the entire project.

---

# Current Development Task

Refactor the existing working Cardputer hardware test into the modular architecture described above.

Create:

```text
src/app/App.h
src/app/App.cpp

src/ui/DisplayManager.h
src/ui/DisplayManager.cpp

src/input/KeyboardManager.h
src/input/KeyboardManager.cpp
```

Refactor:

```text
src/main.cpp
```

Requirements:

1. Preserve the existing display initialization.
2. Preserve the existing keyboard initialization.
3. Preserve working keyboard input.
4. Display typed keyboard characters on the Cardputer display.
5. Keep `main.cpp` minimal.
6. Do not change working hardware configuration unnecessarily.
7. Do not change `platformio.ini` unless required.
8. Build the resulting project with PlatformIO.
9. Fix compilation errors before considering the task complete.

After completion, report:

* files created
* files modified
* architecture changes
* build result
* any assumptions made


## Input Behavior

Unless explicitly requested, keyboard handling must not create a text input
field or persistent text buffer.

The current default behavior is:

- detect newly pressed keys
- expose the pressed key to App
- display the most recently pressed key for debugging/status purposes

Text input fields and editable buffers should only be implemented when a
specific application requires them.

# NETRUN // RED – Product Specification

## Durable multi-ICE invariants

- A Floor is not an Encounter and does not own runtime ICE state.
- A Black ICE floor may spawn up to three independent ICE instances.
- The active ICE collection is fixed-size and owned by GameState.
- Each runtime ICE has a stable identity independent of its type/name.
- Player combat receives an explicit ICE target; new gameplay code must not assume one engaged ICE.
- Pursuing ICE remains independent of the current floor and may act while being chased.

## Project Goal

NETRUN // RED is a Cyberpunk RED Netrunning game/simulator for the
M5Stack Cardputer ADV.

The Cardputer should function as the player's physical Cyberdeck during
a tabletop Cyberpunk RED session.

The application simulates traversal of a NET Architecture, NET Actions,
Programs, Black ICE, Passwords, Files, Control Nodes and other
Netrunning mechanics.

The project should remain usable as a standalone game/simulation while
staying close to the Cyberpunk RED Netrunning rules.

---

# Core Design Principles

## Separation of concerns

Gameplay rules must be independent from the user interface.

The UI must not contain Cyberpunk RED rule logic.

Preferred dependency direction:

UI
↓
GameState
↓
Game Rules
↓
Scenario / Content Data

Game rules should be testable without rendering the Cardputer UI.

---

## Data-driven content

Programs, Black ICE, Floors and Scenario content should be represented
as data wherever possible.

Do not create separate gameplay systems for individual Black ICE or
Programs unless their mechanics require special behavior.

The long-term goal is to load NET Architectures from external scenario
files, potentially from SD card.

V0.1 may use hardcoded test data.

---

# Input / Controls

The application should be fully usable through the Cardputer keyboard.

Primary controls:

Arrow Up / Down:
Navigate menus

Arrow Left / Right:
Navigate where appropriate

Enter:
Confirm selection

Esc:
Return / cancel

Avoid requiring arbitrary letter shortcuts for core gameplay.

Menus should be contextual and only show relevant actions whenever
possible.

---

# UI Direction

The visual style should resemble a Cyberpunk terminal / Cyberdeck.

Use:

- text-based interfaces
- ASCII-style graphics
- simple ASCII animations
- status bars
- short transition animations
- clear NET Action indicators

Animations should use a small number of frames rather than attempting
high-framerate graphics.

Gameplay readability has priority over visual complexity.

Typical gameplay screen layout:

Architecture / Floor information at top

Current NET object / ASCII representation in center

HP / REZ / status information

Context-sensitive action menu at bottom

---

# Cyberpunk RED Rules – Core Assumptions

The Cyberpunk RED Corebook is the rules reference for Netrunning.

Important implementation rules:

- Interface Rank determines available NET Actions.
- Interface 1–3: 2 NET Actions
- Interface 4–6: 3 NET Actions
- Interface 7–9: 4 NET Actions
- Interface 10: 5 NET Actions

Movement through a NET Architecture does NOT consume a NET Action.

Movement may be blocked by Obstructions such as Passwords.

Jack In and safe Jack Out consume NET Actions.

Zap is an Interface Ability and not a Cyberdeck Program.

Programs may have the states:

- Inactive
- Rezzed
- Derezzed
- Destroyed

Activating or deactivating a Program consumes a NET Action.

Triggered Black ICE must be modeled independently from its original
Floor because active Black ICE may pursue the Netrunner through the
Architecture.

Do not assume that a Floor and an Encounter are the same object.

---

# Architecture Model

NET Architectures must be modeled independently from rendering.

V0.1 uses a linear Architecture.

The design must allow branching Architectures to be added later without
rewriting the entire game engine.

A Floor should contain at minimum:

- ID
- Floor type
- DV where applicable
- Content reference / ID
- discovered state
- resolved state

Initial Floor types:

- Empty
- Password
- File
- ControlNode
- BlackICE

Movement rules should eventually be determined by the content of the
Floor rather than simply by its resolved state.

---

# Game State

A central GameState should own or coordinate:

- Netrunner
- Cyberdeck
- NET Architecture
- active / pursuing Black ICE
- current run state
- current turn state

Avoid global gameplay state where possible.

---

# V0.1 Scope

V0.1 is a vertical gameplay slice.

The target gameplay flow is:

Boot
→ Start Run
→ Select MILITECH TEST NET
→ Jack In
→ Password
→ Backdoor
→ File
→ Eye-Dee
→ Download
→ Hellhound encounter
→ Sword / Zap / Slide
→ Control Node
→ Control
→ Objective Complete
→ Jack Out
→ Run Complete

---

## V0.1 Test Netrunner

Handle:
REDSHIFT

Interface:
4

HP:
40

NET Actions:
3

---

## V0.1 Cyberdeck

Use a standard 7-slot Cyberdeck model.

Initial Programs:

- Sword
- Armor

Zap must not be represented as a Program.

---

## V0.1 Test Architecture

Name:

MILITECH TEST NET

Floors:

1. Password – DV6
2. File – DV6
3. Hellhound
4. Control Node – DV6

The Architecture is linear in V0.1.

---

# V0.1 Interface Abilities

Implement eventually during V0.1:

- Backdoor
- Eye-Dee
- Control
- Slide
- Zap

Pathfinder may be added after the basic gameplay loop works.

Scanner, Virus and other mechanics are outside the initial vertical
slice.

---

# V0.1 Development Order

Development should proceed incrementally.

1. Game data model
2. GameState
3. Dice system
4. NET rules engine
5. Program mechanics
6. Black ICE mechanics
7. Turn system
8. Basic gameplay UI
9. ASCII animations
10. Full V0.1 test run

Do not implement later steps prematurely unless required by the current
architecture.

Each major step should compile and be testable before continuing.

---

# Explicitly Out of Scope for V0.1

Do not implement unless explicitly requested:

- branching NET Architectures
- random Architecture generation
- external JSON scenario loading
- SD card scenarios
- GM Architecture Builder
- Demons
- enemy Netrunners
- Virus creation
- full Scanner / Meatspace simulation
- multiple Access Points
- character creator
- save games
- complete Program database
- complete Black ICE database

The architecture should allow these features to be added later.

---

# Implementation Guidelines

Prefer simple and explicit C++ suitable for ESP32 embedded development.

Avoid unnecessary dynamic allocation.

Avoid large frameworks or dependencies unless clearly justified.

Keep memory usage appropriate for the Cardputer ADV.

Existing working Cardputer display and keyboard functionality must not
be broken when implementing gameplay systems.

Before considering an implementation task complete:

1. Build the project with PlatformIO.
2. Fix compile errors.
3. Preserve existing working functionality.
4. Do not silently expand the requested scope.

## UI Style

# UI Visual Specification

Primary UI reference:

`docs/ui/netrun-ui-reference-v01.png`

This image defines the intended visual direction for the current
Cardputer gameplay UI.

The reference is a style and layout guide, not a pixel-perfect template.

Target display:

- M5Stack Cardputer ADV
- 240x135 pixels

## Gameplay Screen Layout

Gameplay floor screens should use a consistent structure:

- compact run HUD at the top
- large current NET object on the left
- action menu on the right
- object status information below the object

Status information must not be placed between the object and action menu.

The action panel should remain in a consistent position and width across
different floor types whenever possible.

## Visual Object Language

Password:
Represent as a door.

Security level is communicated visually rather than through an exact DV:

- low security: wooden/simple door
- medium security: reinforced steel door
- high security: vault/safe door

Do not display exact Password DV values to the player.

File / Data Node:
Represent as a classic database stack with multiple cylindrical layers.

Control Node:
Represent as a technical server/control tower with panels, antennas and
system details.

Black ICE:
Use distinctive detailed sprites.
Hellhound should be clearly recognizable as an aggressive cybernetic
wolf/dog rather than a generic icon.

## Status Placement

Object-specific information belongs below the object whenever possible.

Examples:

Hellhound:
- Status
- REZ bar

Data Node:
- Status or identified file metadata

Control Node:
- Hostile / Owned state

Password:
- Access Locked / Access Open

Avoid placing object status text beside the object where it can collide
with the action menu.

## UI Color Palette

Preferred palette:

- Purple/Magenta: system UI, navigation, data, active selection
- Red: Black ICE, hostile state, danger, damage
- Green: success, identified, owned, completed
- Yellow: warning and security cues
- White/Gray: neutral text, borders, secondary information

Bright cyan should not be used as the dominant interface color.

## Rendering Priorities

For the 240x135 display:

1. readability
2. consistent layout
3. clear object identity
4. status visibility
5. visual detail

If space becomes constrained, simplify sprite detail before removing
important gameplay information.

Gameplay and rules logic must never be implemented inside visual render
code.

## Animation Timing

Gameplay feedback must remain clearly readable on the physical
Cardputer display.

Animations should favor perceptibility over speed.

Typical visual feedback should remain visible for roughly 500–800 ms.
Dice/check animations may run for roughly 700–1000 ms total.
The Jack-In sequence may run for roughly 1.5–2 seconds.

Avoid extremely fast transitions that are difficult to perceive on
real hardware.
