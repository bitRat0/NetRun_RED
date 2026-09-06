NETRUN // RED - R1.2e recovery candidate
Generated from:
- current damaged project archive (post failed R1.2f)
- intact 2026-09-01 project snapshot
- compiled pre-corruption firmware.elf string data
- known R1.2a-R1.2e project checkpoints/regression output

What was repaired:
- failed R1.2f technical renames reversed to R1.2e names
- all broken literal-mask placeholders removed/restored
- R1.2a-e additions retained (VisualIds, content identity, behavior/animation routing,
  Player ICE definition pointer, Demon stable/display identity, Sprite Review graph fix/tests)
- R1.2f-only explicit enum/Count shape changes reverted

Verification status:
- Static recovery checks passed: no literal-mask placeholders remain and quoted project includes resolve.
- NOT compiled in the recovery environment.
- MUST be verified locally with PlatformIO before replacing the broken project permanently.

Recommended validation:
1. Keep the original damaged project as a separate backup.
2. Replace ONLY the project's src directory with this recovered src directory.
3. platformio.ini in this candidate has NETRUN_RUN_BOOT_TESTS=1 (matching the uploaded current project).
4. Clean build.
5. Flash and run the complete boot regression suite.
6. Required checkpoints include ContentIdentity, VisualId, BehaviorId, AnimationId,
   PlayerIceIdentity, PlayerBlackIce*, DemonIdentity, Demon, SpriteReviewNullbyteDormant,
   and Integration = PASS.
7. Once hardware regression passes, set NETRUN_RUN_BOOT_TESTS=0 and make a clean release build.
8. Initialize local Git and commit this recovered R1.2e baseline before retrying R1.2f.
