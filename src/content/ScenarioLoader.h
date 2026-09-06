#pragma once

#include <Arduino.h>
#include <FS.h>

#include "ScenarioImport.h"

class ScenarioLoader
{
public:
    static constexpr size_t JSON_CAPACITY = 8192;
    ScenarioLoader() : registry_(blackIceRegistry()), demonRegistry_(::demonRegistry()), enemyRegistry_(::enemyNetrunnerRegistry()) {}
    explicit ScenarioLoader(const BlackIceRegistry& registry) : registry_(registry), demonRegistry_(::demonRegistry()), enemyRegistry_(::enemyNetrunnerRegistry()) {}
    ScenarioLoader(const BlackIceRegistry& registry, const DemonRegistry& demons) : registry_(registry), demonRegistry_(demons), enemyRegistry_(::enemyNetrunnerRegistry()) {}
    ScenarioLoader(const BlackIceRegistry& registry, const DemonRegistry& demons, const EnemyNetrunnerRegistry& enemies) : registry_(registry), demonRegistry_(demons), enemyRegistry_(enemies) {}

    ScenarioImportResult loadJson(const char* json, LoadedScenario& output) const;
    ScenarioImportResult loadStream(Stream& stream, LoadedScenario& output) const;
    ScenarioImportResult loadFile(fs::FS& fs, const char* path, LoadedScenario& output) const;

private:
    const BlackIceRegistry& registry_;
    const DemonRegistry& demonRegistry_;
    const EnemyNetrunnerRegistry& enemyRegistry_;
};
