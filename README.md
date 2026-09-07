# NETRUN // RED

**A standalone handheld Netrunning simulator for the M5Stack Cardputer ADV.**

NETRUN // RED turns the Cardputer ADV into a compact cyberdeck for running
data-driven NET Architectures at the table. Navigate branching networks, break
passwords, access Data and Control Nodes, encounter hostile Black ICE and Enemy
Netrunners, deploy Player Black ICE, face Demons, and create your own runs with
the included offline Scenario Builder.

No server, account, cloud service, or internet connection is required to play or
use the Builder.

> \*\*Version 1.0.0\*\*  
> Target hardware: \*\*M5Stack Cardputer ADV / ESP32-S3\*\*  
> Source available under the \*\*PolyForm Noncommercial License 1.0.0\*\*

### 

### !\[NETRUN // RED on Cardputer ADV](docs/images/netrun-red-multishow.jpg)



## Features

* Standalone Netrunning gameplay on the M5Stack Cardputer ADV
* Linear and branching NET Architectures
* Passwords, Data Nodes, and Control Nodes
* Black ICE encounters and Player Black ICE
* Enemy Netrunners with multiple behavior profiles
* Demon-controlled Architectures
* Scenario-local and reusable catalog definitions
* Custom Black ICE, Demon, and Enemy Netrunner JSON
* SD-card based Scenario loading
* Offline graphical Scenario Builder
* Optional external 320 x 240 ILI9341 display
* No backend, account, cloud service, or telemetry required



!\[NETRUN // RED on Cardputer ADV](docs/images/netrun-red-fight.jpg)





## Target hardware

NETRUN // RED V1 targets the
[M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv).

The Cardputer ADV provides the ESP32-S3 platform, built-in 240 x 135 display,
keyboard, microSD slot, battery, and expansion interface used by the project.

### Optional external display

NETRUN // RED can also mirror the internal Cardputer display to an external
**ILI9341 320 x 240 SPI display**.

This optional display setup is closely related to the external-display work by
**AIAndy / AndyAiCardputer**. His Cardputer ADV + ILI9341 projects are useful
references for the hardware setup and for obtaining a printable display enclosure:

* [Cardputer ADV + ILI9341 project on GitHub](https://github.com/AndyAiCardputer/zx-spectrum-cardputer-ili9341)
* [External Display Case for Cardputer Adv ILI9341 on MakerWorld](https://makerworld.com/en/models/2164134-external-display-case-for-cardputer-adv-ili9341) — STL files can be downloaded from the model page
* [AIAndy / AndyLong14 on YouTube](https://www.youtube.com/@andylong14)
* [External display case / assembly video](https://www.youtube.com/watch?v=-N_J1kQnoHg)

Check the dimensions of your specific ILI9341 module against the enclosure before
printing, as display PCBs are sold in several physical sizes.

The current firmware uses:

|Signal|GPIO|
|-|-:|
|TFT CS|5|
|TFT DC|6|
|TFT RST|3|
|SPI SCK|40|
|SPI MOSI|14|
|TFT MISO|Not used|

Use the Cardputer ADV expansion connector for power and ground according to the
official M5Stack pinout.

The external display mirrors the 240 x 135 internal screen as a centered
320 x 180 image and reserves a small lower strip for ambient presentation.

## Install, build, and flash

V1 is distributed as source. The normal installation path is to build and upload
the firmware directly to the Cardputer ADV with **Visual Studio Code +
PlatformIO IDE**.

### 1\. Install Visual Studio Code

Download and install
[Visual Studio Code](https://code.visualstudio.com/).

### 2\. Install PlatformIO IDE inside VS Code

Open VS Code and:

1. Open **Extensions** with `Ctrl+Shift+X`.
2. Search for **PlatformIO IDE**.
3. Install the extension published by **PlatformIO**.
4. Restart VS Code if requested.

Official PlatformIO documentation:
[PlatformIO IDE for VS Code](https://docs.platformio.org/en/latest/integration/ide/pioide.html)

You do **not** need to install PlatformIO Core separately when using the
PlatformIO IDE extension. The extension includes PlatformIO Core and its tools.

This repository also contains `.vscode/extensions.json`, so VS Code may
automatically recommend the `platformio.platformio-ide` extension when the
project is opened.

### 3\. Open the project

In VS Code:

1. Select **File -> Open Folder...**
2. Open the NETRUN // RED project folder containing `platformio.ini`.
3. Wait for PlatformIO to initialize the project and install its required
platform and libraries.

The V1 PlatformIO environment is:

```ini
\[env:m5stack-cardputer]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
```

### 4\. Choose your display configuration

Open `platformio.ini` and locate:

```ini
-DENABLE\_EXTERNAL\_DISPLAY=1
```

For a **Cardputer ADV without the optional external display**, change it to:

```ini
-DENABLE\_EXTERNAL\_DISPLAY=0
```

For a **Cardputer ADV with the ILI9341 external display**, leave it at:

```ini
-DENABLE\_EXTERNAL\_DISPLAY=1
```

Do not change other build flags unless you know why they are required.

### 5\. Put the Cardputer ADV into flash mode

The Cardputer ADV must be in flash/download mode before uploading the firmware.

1. Disconnect the USB cable from the Cardputer ADV.
2. Press and hold the **G0 / GO button**.
3. While continuing to hold the button, connect the USB data cable to your computer.
4. Keep the button held briefly while the device enters flash mode, then release it.
5. Start the PlatformIO upload.

If the Cardputer is connected normally without entering flash mode first, the
firmware upload may fail.

For the official M5Stack programming procedure, see:
[Cardputer ADV firmware programming](https://docs.m5stack.com/en/uiflow2/cardputer-adv/program)

### 6\. Build

The easiest method for new users is the PlatformIO toolbar in VS Code:

* **Build**: click the PlatformIO **checkmark**
* or open the Command Palette and run **PlatformIO: Build**

You can also build from the PlatformIO terminal:

```bash
pio run
```

### 7\. Upload to the Cardputer ADV

With the Cardputer connected:

* click the PlatformIO **Upload** arrow
* or run **PlatformIO: Upload** from the Command Palette

The equivalent terminal command is:

```bash
pio run --target upload
```

After a successful upload, the Cardputer will reboot into NETRUN // RED.

## SD card setup

NETRUN // RED loads custom Scenarios and reusable catalogs from the Cardputer
microSD card.

Create these paths on the SD card:

```text
/scenarios/
/catalog/
```

Scenario and catalog files use these destinations:

```text
/scenarios/<scenario>.json
/catalog/black\_ice.json
/catalog/demons.json
/catalog/enemies.json
```

A public starter set is provided under `examples/`:

```text
examples/
├── catalog/
│   ├── black\_ice.json
│   ├── demons.json
│   └── enemies.json
└── scenarios/
    └── basic\_training\_net\_full.json
```

Copy the catalog files into `/catalog/` and the Scenario JSON into
`/scenarios/`.

The included **BASIC TRAINING NET** demonstrates core V1 Scenario features,
including a Password, Data Node, Control Node, Black ICE, an Enemy Netrunner,
and the homebrew Demon `LATCH`.

## Controls

NETRUN // RED can be operated entirely from the Cardputer keyboard.

|Action|Key|
|-|-|
|Up|Arrow Up / `;`|
|Down|Arrow Down / `.`|
|Left|Arrow Left / `,`|
|Right|Arrow Right / `/`|
|Confirm / Select|`Enter`|
|Back|`Esc` or `Backspace`|
|Item information|`I` where shown|

The Cardputer arrow keys are available on the keyboard's Fn layer. NETRUN // RED
also accepts the corresponding base keys `;`, `.`, `,`, and `/` directly for
navigation.

## Basic operation

### Configure your Runner

From the main interface you can review or edit the Runner profile and configure
the Cyberdeck before starting a run.

The Cyberdeck menu provides installed Software, Hardware, and Player Black ICE.

When a **Program** or **Player Black ICE** entry is highlighted, press `I` to open
its information screen. This lets you review the item's details before installing,
activating, or selecting it.

### Select a Run

Choose **SELECT RUN** to browse available built-in and SD-card Scenarios.

The SD Scenario list reads files from:

```text
/scenarios/
```

Select a Scenario and confirm **JACK IN** to begin.

### Navigate the Architecture

During a run:

* use Up/Down to select available NET Actions
* use Left/Right to change focus when multiple entities are present on a Floor
* confirm an action with Enter
* use Back to leave menus or return to the previous screen
* use the Architecture Map and movement options to navigate branching Scenarios

Available actions depend on the current Floor, discovered content, active
programs, hostile entities, and current game state.

### Run completion

A run may contain files to identify/download, Control Nodes, Passwords, hostile
entities, and an optional final objective. The final Floor can present the
Virus-placement decision when the Scenario reaches its completion state.

## Offline Scenario Builder

NETRUN // RED includes a graphical authoring tool under:

```text
tools/scenario-builder/
```

### No installation required

Open:

```text
tools/scenario-builder/index.html
```

directly in a current desktop browser.

The Builder runs locally through `file://`. It does not require a web server,
account, backend, cloud connection, telemetry, or internet access.

Keep these runtime files together:

```text
index.html
style.css
firmware-data.js
model.js
preview.js
app.js
```

### What the Builder can create

The Builder authors and validates:

* Scenarios
* Black ICE catalogs
* Demon catalogs
* Enemy Netrunner catalogs

It supports:

* text and graphical Architecture editing
* branches, merges, links, and draggable graph nodes
* mixed Floor content
* catalog and Scenario-local definitions
* Black ICE and Demon presentation previews
* firmware-bound validation
* JSON import and export
* browser-local workspace autosave and restore

### Import and export

Use **Import** to load an existing supported JSON document.

Use **Download** to export the active document as portable firmware JSON. The
Builder does not write directly to the Cardputer or its SD card; exported files
must be copied to the correct SD path manually.

Browser-local workspace data is only used by the Builder and is not included in
exported JSON.

For the complete authoring contract, see
[`tools/scenario-builder/README.md`](tools/scenario-builder/README.md).

## Release files

The public V1 release is intended to provide the firmware source together with
separate convenience packages for the offline Builder and SD starter content.

See the repository's [GitHub Releases](../../releases) page for the published
V1.0.0 release files and checksums once the release is available.

## Credits

NETRUN // RED was created for the
[M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv).

Special thanks to
[AIAndy / AndyLong14](https://www.youtube.com/@andylong14)
for Cardputer-related projects, experimentation, and community inspiration.

Third-party software and dependency notices are listed in
[`docs/THIRD\_PARTY\_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md).

## License

Original NETRUN // RED project material is:

**Source available under the**
[**PolyForm Noncommercial License 1.0.0**](LICENSE)**.**

In short, the public license permits non-commercial:

* use
* modification
* forks
* redistribution

Commercial use is not granted by the public license and requires separate
permission from the project owner.

This summary is provided for convenience only. The full text in `LICENSE`
governs use of the project.

User-created Scenario, Black ICE, Demon, Enemy Netrunner, and supported
configuration/data JSON remains User Content.

NETRUN // RED is **source available**, not OSI-approved Open Source software.

## Unofficial fan-project notice

NETRUN // RED is an unofficial, non-commercial fan/homebrew project. It is not
endorsed by or affiliated with R. Talsorian Games or CD PROJEKT RED.

Cyberpunk, Cyberpunk RED, and related trademarks, settings, characters, artwork,
rules content, and other intellectual property belong to their respective owners.

The NETRUN // RED project license applies only to original project material. It
does not grant rights to third-party Cyberpunk intellectual property.

This repository does not redistribute Cyberpunk RED rulebooks, official artwork,
or official statblock-based Black ICE or Demon preset content. Users are
responsible for owning and using legitimate Cyberpunk RED materials where
applicable.

