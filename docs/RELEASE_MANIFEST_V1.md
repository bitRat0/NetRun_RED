# V1 public release manifest

This defines the intended release archive after final Cardputer ADV hardware acceptance succeeds.

## Include

- Firmware source: src/, include/, lib/, platformio.ini, and required build files.
- Public documentation: README.md, docs/NETRUN_PROJECT_STATUS.md,
  docs/NETRUN_RELEASE_BASELINE.md, docs/RELEASE_V1.md,
  docs/THIRD_PARTY_NOTICES.md, and current Scenario/catalog contract documents.
- Offline Builder: tools/scenario-builder runtime files, README, development
  verification scripts, and its homebrew examples.
- Public-safe SD starter set: examples/catalog and examples/scenarios.
- LICENSE and applicable upstream notices.

## Exclude

- .pio/, IDE state, temporary logs, generated local browser profiles, and personal
  paths or credentials.
- RECOVERY_README.txt and archived recovery material.
- docs/scenarios/examples: historical parser/hardware fixtures, including legacy
  Black ICE names; they are test/documentation inputs, not public starter content.
- Invalid sample JSON except where deliberately supplied as clearly labeled
  developer-test fixtures.
- Cyberpunk RED PDFs, rulebook extracts, copied artwork, official statblock-based
  Black ICE/Demon presets, private/home-group scenarios, and secrets.
- Prebuilt firmware binaries unless a later release step intentionally adds a
  binary plus the required license/notices.

## Release checks

PROJECT LICENSE: RESOLVED — LICENSE contains the PolyForm Noncommercial License 1.0.0.