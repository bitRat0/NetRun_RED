# NETRUN // RED

NETRUN // RED is an unofficial, homebrew Netrunning simulator for the M5Stack
Cardputer ADV. It runs as custom ESP32-S3 firmware and supports data-driven NET
Architectures, Black ICE, Enemy Netrunners, Demons, Player Black ICE, and an
offline Scenario Builder.

## Hardware and build

Supported hardware: M5Stack Cardputer ADV / ESP32-S3.

Install PlatformIO, open this repository, and build the configured environment:

    pio run

Flash only after a successful build:

    pio run --target upload

The checked-in configuration keeps NETRUN_RUN_BOOT_TESTS=1 for release-candidate
hardware validation. Do not disable it for the RC hardware run.

## SD content

Copy Scenario and catalog JSON to the Cardputer SD card:

    /scenarios/<scenario>.json
    /catalog/black_ice.json
    /catalog/demons.json
    /catalog/enemies.json

Use SELECT RUN to choose a Scenario. Schema Version 1 supports linear and
branching Architectures, mixed neutral Floor content plus Black ICE/Enemy
placement, catalog references, and one selected Demon. Current Custom Demon
presentation values are orb, sentinel, crown and lunge, pulse, slash, burst.

The public-safe starter material is under examples/. It contains project-owned
homebrew definitions and current Schema 1 examples. The historical
docs/scenarios/examples fixtures are test/documentation material and are not
release-package samples.

## Offline Scenario Builder

Open tools/scenario-builder/index.html directly in a current desktop browser.
It is a fully offline file:// tool: no server, account, cloud, telemetry, or
HTTP(S) dependency is required.

The Builder authors Scenarios and Black ICE, Demon, and Enemy Netrunner catalogs;
validates device limits; edits text/SVG architecture graphs; previews Black ICE
and Demon visuals/animations; and imports/exports portable JSON. It autosaves a
browser-local workspace, restores it on reopening, preserves catalogs with New
Scenario, and can Clear local workspace. Local workspace metadata is never
included in exported firmware JSON.

See tools/scenario-builder/README.md for the full authoring contract.

## Project status and release candidate

Current capabilities, known limitations, and release checks are documented in
docs/NETRUN_PROJECT_STATUS.md. V1 is a public release candidate awaiting the
final user hardware run; no V1.0.0 tag exists yet.
## Unofficial fan-project notice

NETRUN // RED is an unofficial fan/homebrew project. It is not endorsed by
R. Talsorian Games. Cyberpunk and Cyberpunk RED-related trademarks and intellectual
property belong to their respective owners. This project does not redistribute
official rules, rulebooks, artwork, or official statblock-based Black ICE/Demon
preset content. Users are responsible for owning and using legitimate Cyberpunk
RED materials where applicable.

## License and contributions

NETRUN // RED original project material is source available under the [PolyForm Noncommercial License 1.0.0](LICENSE).

Non-commercial use, installation, modification, forks, and redistribution are permitted subject to that license. Commercial rights are not granted; commercial use requires separate permission from the project owner. User-created Scenario, Black ICE, Demon, Enemy Netrunner, and supported configuration/data JSON remains User Content.

This project is source available, not Open Source or OSI-approved Open Source.
See docs/THIRD_PARTY_NOTICES.md for dependency notices and
docs/RELEASE_MANIFEST_V1.md for release-package contents.