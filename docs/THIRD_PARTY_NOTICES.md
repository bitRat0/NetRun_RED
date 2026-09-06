# Third-party notices

## Project material

Original NETRUN // RED source, project-owned documentation, graphics, and assets are covered by the PolyForm Noncommercial License 1.0.0 in LICENSE.

This repository does not vendor third-party library source. PlatformIO resolves
the firmware dependencies declared in platformio.ini. A binary release must retain
the applicable upstream notices and license terms for the exact resolved versions.

Observed release-candidate dependencies:

- M5Cardputer 1.1.1, by M5Stack/Sean; it brings M5Unified, M5GFX, and IRremote.
  The installed package did not contain a top-level license file, so its upstream
  license must be confirmed when assembling a public binary archive.
- ArduinoJson 6.21.5, by Benoit Blanchon, MIT License.
- Adafruit GFX Library and Adafruit ILI9341, BSD-style licenses from Adafruit.
  Their transitive display dependencies carry their own upstream notices.

The project license does not grant rights to Cyberpunk, Cyberpunk RED, R. Talsorian Games, CD PROJEKT, or any other third-party IP. Those rights remain with their respective holders. This notice does not replace preserving upstream notices in a binary distribution.