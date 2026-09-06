# Scenario and SD catalog contract

The public Scenario format is netrun-architecture with Schema Version 1. Missing
or unsupported schema versions are rejected.

Reusable custom definitions belong in /catalog/black_ice.json,
/catalog/demons.json, and /catalog/enemies.json. Scenario files contain
Architecture data and stable-ID references. Scenario-local black_ice, demons, and
enemies arrays remain supported for self-contained scenarios. Resolution is local,
then global catalog, then built-in where applicable. Duplicate IDs and attempts to
shadow built-ins are rejected.

Catalog envelopes use netrun-black-ice-catalog, netrun-demon-catalog, and
netrun-enemy-catalog with version 1 and their matching entry arrays.

Copy public-safe catalog and Scenario files to:

    /catalog/black_ice.json
    /catalog/demons.json
    /catalog/enemies.json
    /scenarios/<scenario>.json

## Mixed Floor placement

Neutral empty, password, file, and control Floor data is independent from hostile
placement. ice may contain up to three stable IDs and enemy is an independent
optional stable ID; both can coexist with a neutral Floor object. A Demon is
selected by the Scenario-root demon reference, not by Floor placement.

The Builder exports reusable entities as catalogs and Scenarios as references by
default; its explicit local-definition workflow exports portable self-contained
Scenario data. Its local browser workspace is not part of exported JSON.
