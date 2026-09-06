#include "ScenarioLoader.h"

#include <ArduinoJson.h>
#include <ctype.h>

#include "game/ProgramCatalog.h"

namespace
{
ScenarioImportResult fail(ScenarioImportError error, const char* message,
                          int floorIndex = -1, int floorId = -1)
{
    ScenarioImportResult result;
    result.error = error; result.message = message;
    result.floorIndex = floorIndex; result.floorId = floorId;
    return result;
}

LoadedScenario& scenarioImportStaging()
{
    static LoadedScenario staging;
    return staging;
}

bool copyText(const char* source, char* destination, size_t capacity)
{
    if (source == nullptr || strlen(source) >= capacity) return false;
    strcpy(destination, source);
    return true;
}

bool validDemonId(const char* id)
{
    if (id == nullptr || id[0] == '\0' || strlen(id) >= DemonInstance::MAX_STABLE_ID_LENGTH + 1) return false;
    for (const char* cursor = id; *cursor; ++cursor)
        if (!(islower(static_cast<unsigned char>(*cursor)) || isdigit(static_cast<unsigned char>(*cursor)) ||
              *cursor == '_' || *cursor == '-')) return false;
    return true;
}

bool validEnemyId(const char* id)
{
    if (id == nullptr || id[0] == '\0' || strlen(id) >= EnemyNetrunnerRegistry::ID_LENGTH) return false;
    for (const char* cursor = id; *cursor; ++cursor)
        if (!(islower(static_cast<unsigned char>(*cursor)) || isdigit(static_cast<unsigned char>(*cursor)) ||
              *cursor == '_' || *cursor == '-')) return false;
    return true;
}

const char* optionalText(JsonObjectConst object, const char* key)
{
    return object[key].is<const char*>() ? object[key].as<const char*>() : nullptr;
}

bool parseProgramName(const char* value, ProgramId& output)
{
    if (value == nullptr) return false;
    for (uint8_t raw = static_cast<uint8_t>(ProgramId::Sword);
         raw < static_cast<uint8_t>(ProgramId::Count); ++raw)
    {
        const ProgramId id = static_cast<ProgramId>(raw);
        const ProgramDefinition* definition = programDefinition(id);
        if (definition != nullptr && !strcmp(value, definition->name))
        {
            output = id;
            return true;
        }
    }
    return false;
}

bool parseDemonAnimation(const char* value, HostileAttackStyle& output) { if (value == nullptr) return false; if (!strcmp(value, "lunge")) output = HostileAttackStyle::Lunge; else if (!strcmp(value, "burst")) output = HostileAttackStyle::Burst; else if (!strcmp(value, "slash")) output = HostileAttackStyle::Slash; else if (!strcmp(value, "pulse")) output = HostileAttackStyle::Pulse; else return false; return true; }

bool parseEnemyBehavior(const char* value, EnemyBehaviorId& output)
{
    if (value == nullptr) return false;
    if (!strcmp(value, "baseline")) output = EnemyBehaviorId::Baseline;
    else if (!strcmp(value, "defensive")) output = EnemyBehaviorId::Defensive;
    else if (!strcmp(value, "sentry")) output = EnemyBehaviorId::Sentry;
    else return false;
    return true;
}

const EnemyNetrunnerDefinition* resolveEnemyDefinition(const LoadedScenario& scenario,
                                                        const EnemyNetrunnerRegistry& registry,
                                                        const char* id)
{
    const EnemyNetrunnerDefinition* local = nullptr;
    if (id != nullptr)
        for (size_t index = 0; index < scenario.enemyDefinitionCount(); ++index)
            if (scenario.enemyDefinitions()[index].id != nullptr &&
                !strcmp(scenario.enemyDefinitions()[index].id, id))
                local = &scenario.enemyDefinitions()[index];
    if (local != nullptr) return local;
    const EnemyNetrunnerDefinition* global = registry.findByStableId(id);
    if (global != nullptr) return global;
    return enemyNetrunnerDefinitionById(id);
}

ScenarioImportResult parseLocalBlackIce(JsonObjectConst root, LoadedScenario& output,
                                        const BlackIceRegistry& registry)
{
    JsonVariantConst rawDefinitions = root["black_ice"];
    if (rawDefinitions.isNull()) { ScenarioImportResult result; result.success = true; return result; }
    if (!rawDefinitions.is<JsonArrayConst>()) return fail(ScenarioImportError::InvalidBlackIceDefinition, "INVALID LOCAL ICE");
    JsonArrayConst definitions = rawDefinitions.as<JsonArrayConst>();
    if (definitions.size() > LoadedScenario::MAX_LOCAL_BLACK_ICE)
        return fail(ScenarioImportError::TooManyBlackIce, "TOO MANY LOCAL ICE");
    size_t count = 0;
    for (JsonVariantConst raw : definitions) {
        if (!raw.is<JsonObjectConst>()) return fail(ScenarioImportError::InvalidBlackIceDefinition, "INVALID LOCAL ICE");
        BlackIceCatalogResult parsed = parseBlackIceDefinition(raw.as<JsonObjectConst>(), output.localBlackIce()[count]);
        if (!parsed.success) return fail(ScenarioImportError::InvalidBlackIceDefinition, parsed.message);
        const char* id = output.localBlackIce()[count].id;
        if (blackIceDefinitionByStableId(id) != nullptr)
            return fail(ScenarioImportError::BlackIceIdConflict, "BUILTIN ICE CONFLICT");
        if (registry.findCustomByContentId(id) != nullptr)
            return fail(ScenarioImportError::BlackIceIdConflict, "GLOBAL ICE CONFLICT");
        for (size_t previous = 0; previous < count; ++previous)
            if (!strcmp(output.localBlackIce()[previous].id, id))
                return fail(ScenarioImportError::DuplicateBlackIceId, "DUPLICATE LOCAL ICE");
        ++count;
    }
    output.setLocalBlackIceCount(count);
    ScenarioImportResult result; result.success = true; return result;
}

ScenarioImportResult parseLocalDemons(JsonObjectConst root, LoadedScenario& output,
                                      const DemonRegistry& registry)
{
    JsonVariantConst rawDefinitions = root["demons"];
    if (rawDefinitions.isNull()) { ScenarioImportResult result; result.success = true; return result; }
    if (!rawDefinitions.is<JsonArrayConst>()) return fail(ScenarioImportError::InvalidDemonDefinition, "INVALID LOCAL DEMONS");
    JsonArrayConst definitions = rawDefinitions.as<JsonArrayConst>();
    if (definitions.size() > LoadedScenario::MAX_LOCAL_DEMONS)
        return fail(ScenarioImportError::TooManyDemons, "TOO MANY LOCAL DEMONS");
    size_t count = 0;
    for (JsonVariantConst raw : definitions)
    {
        if (!raw.is<JsonObjectConst>()) return fail(ScenarioImportError::InvalidDemonDefinition, "INVALID LOCAL DEMON");
        JsonObjectConst source = raw.as<JsonObjectConst>();
        const char* id = optionalText(source, "id");
        const char* name = optionalText(source, "name");
        if (!validDemonId(id) || name == nullptr || name[0] == '\0')
            return fail(ScenarioImportError::MissingField, "DEMON ID OR NAME");
        if (strlen(name) >= DemonInstance::MAX_DISPLAY_NAME_LENGTH + 1)
            return fail(ScenarioImportError::StringTooLong, "DEMON NAME TOO LONG");
        if (!source["interface"].is<int>() || !source["rez"].is<int>() || !source["actions"].is<int>())
            return fail(ScenarioImportError::MissingField, "DEMON STATS");
        const int interfaceRank = source["interface"].as<int>();
        const int rez = source["rez"].as<int>();
        const int actions = source["actions"].as<int>();
        if (interfaceRank < 1 || interfaceRank > 10 || rez < 1 || actions < 1 || actions > DemonInstance::MAX_ACTIONS)
            return fail(ScenarioImportError::InvalidDemonDefinition, "INVALID DEMON STATS");
        if (demonDefinitionByStableId(id) != nullptr || registry.findByStableId(id) != nullptr)
            return fail(ScenarioImportError::DemonIdConflict, "DEMON ID CONFLICT");
        for (size_t previous = 0; previous < count; ++previous)
            if (!strcmp(output.localDemons()[previous].stableId, id))
                return fail(ScenarioImportError::DuplicateDemonId, "DUPLICATE LOCAL DEMON");

        DemonVisualId visual = DemonVisualId::Orb01; HostileAttackStyle animation = HostileAttackStyle::Pulse;
        if (!source["visual"].isNull() && (!source["visual"].is<const char*>() || !parseDemonVisualId(source["visual"].as<const char*>(), visual))) return fail(ScenarioImportError::InvalidDemonDefinition, "UNKNOWN DEMON VISUAL");
        if (!source["animation"].isNull() && (!source["animation"].is<const char*>() || !parseDemonAnimation(source["animation"].as<const char*>(), animation))) return fail(ScenarioImportError::InvalidDemonDefinition, "UNKNOWN DEMON ANIMATION");
        DemonStoredDefinition& target = output.localDemons()[count];
        strcpy(target.stableId, id); strcpy(target.displayName, name);
        target.definition = {target.stableId, target.displayName, DemonType::None, visual, animation, rez,
            static_cast<uint8_t>(interfaceRank), static_cast<uint8_t>(actions), 0};
        ++count;
    }
    output.setLocalDemonCount(count);
    ScenarioImportResult result; result.success = true; return result;
}

const BlackIceDefinition* resolveBlackIceDefinition(const LoadedScenario& scenario,
                                                    const BlackIceRegistry& registry, const char* id)
{
    const BlackIceDefinition* local = scenario.findLocalBlackIce(id);
    return local != nullptr ? local : registry.findByContentId(id);
}

const DemonDefinition* resolveDemonDefinition(const LoadedScenario& scenario,
                                              const DemonRegistry& registry, const char* id)
{
    const DemonDefinition* local = scenario.findLocalDemon(id);
    if (local != nullptr) return local;
    const DemonDefinition* global = registry.findByStableId(id);
    if (global != nullptr) return global;
    return demonDefinitionByStableId(id);
}

ScenarioImportResult parseEnemies(JsonObjectConst root, LoadedScenario& output,
                                  const EnemyNetrunnerRegistry& registry)
{
    JsonVariantConst rawEnemies = root["enemies"];
    if (rawEnemies.isNull()) { ScenarioImportResult result; result.success = true; return result; }
    if (!rawEnemies.is<JsonArrayConst>()) return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMIES");
    JsonArrayConst enemies = rawEnemies.as<JsonArrayConst>();
    if (enemies.size() > LoadedScenario::MAX_SCENARIO_ENEMIES)
        return fail(ScenarioImportError::TooManyEnemies, "TOO MANY ENEMIES");

    size_t index = 0;
    for (JsonVariantConst item : enemies)
    {
        if (!item.is<JsonObjectConst>()) return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMY", index);
        JsonObjectConst source = item.as<JsonObjectConst>();
        const char* id = optionalText(source, "id");
        const char* handle = optionalText(source, "handle");
        if (id == nullptr || id[0] == '\0' || handle == nullptr || handle[0] == '\0')
            return fail(ScenarioImportError::MissingField, "ENEMY ID OR HANDLE", index);
        if (strlen(id) >= LoadedScenario::ENEMY_ID_LENGTH ||
            strlen(handle) >= LoadedScenario::ENEMY_HANDLE_LENGTH)
            return fail(ScenarioImportError::StringTooLong, "ENEMY TEXT TOO LONG", index);
        if (!validEnemyId(id))
            return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMY ID", index);
        if (enemyNetrunnerDefinitionById(id) != nullptr || registry.findByStableId(id) != nullptr)
            return fail(ScenarioImportError::EnemyIdConflict, "ENEMY ID CONFLICT", index);
        for (size_t previous = 0; previous < index; ++previous)
            if (output.enemyDefinitions()[previous].id != nullptr &&
                !strcmp(output.enemyDefinitions()[previous].id, id))
                return fail(ScenarioImportError::DuplicateEnemyId, "DUPLICATE ENEMY ID", index);
        if (!source["interface"].is<int>() || !source["hp"].is<int>() ||
            !source["actions"].is<int>() || !source["programs"].is<JsonArrayConst>())
            return fail(ScenarioImportError::MissingField, "ENEMY STATS OR PROGRAMS", index);

        const int interfaceRank = source["interface"].as<int>();
        const int hp = source["hp"].as<int>();
        const int actions = source["actions"].as<int>();
        JsonArrayConst programs = source["programs"].as<JsonArrayConst>();
        if (interfaceRank < 1 || interfaceRank > 10 || hp < 1 || hp > 255 ||
            actions < 1 || actions > 5 || programs.size() > MAX_CYBERDECK_PROGRAMS)
            return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMY STATS", index);

        LoadedScenario::EnemyStrings& strings = output.enemyStrings(index);
        if (!copyText(id, strings.id, sizeof(strings.id)) ||
            !copyText(handle, strings.handle, sizeof(strings.handle)))
            return fail(ScenarioImportError::StringTooLong, "ENEMY TEXT TOO LONG", index);
        EnemyNetrunnerDefinition& target = output.enemyDefinitions()[index];
        target.id = strings.id;
        target.name = strings.handle;
        target.interfaceRank = static_cast<uint8_t>(interfaceRank);
        target.maxHp = static_cast<uint8_t>(hp);
        target.netActions = static_cast<uint8_t>(actions);
        target.deckConfig = CyberdeckConfig();
        target.deckConfig.quality = CyberdeckQuality::Standard;
        target.deckConfig.programCount = 0;
        for (JsonVariantConst rawProgram : programs)
        {
            if (!rawProgram.is<const char*>())
                return fail(ScenarioImportError::UnknownEnemyProgram, "INVALID ENEMY PROGRAM", index);
            ProgramId program = ProgramId::None;
            if (!parseProgramName(rawProgram.as<const char*>(), program))
                return fail(ScenarioImportError::UnknownEnemyProgram, "UNKNOWN ENEMY PROGRAM", index);
            target.deckConfig.programs[target.deckConfig.programCount++] = program;
        }
        const char* ai = optionalText(source, "ai");
        if (ai != nullptr && strcmp(ai, "anti_personnel") != 0)
            return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMY AI", index);
        target.ai = EnemyAiArchetype::AntiPersonnel;
        target.stationaryUntilDiscovered = false;
        target.behavior = EnemyBehaviorId::Baseline;
        if (!source["behavior"].isNull())
        {
            if (!source["behavior"].is<const char*>() ||
                !parseEnemyBehavior(source["behavior"].as<const char*>(), target.behavior))
                return fail(ScenarioImportError::InvalidEnemyBehavior, "INVALID ENEMY BEHAVIOR", index);
        }
        if (!source["dormant_until_discovered"].isNull() &&
            !source["dormant_until_discovered"].is<bool>())
            return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMY DORMANCY", index);
        if (source["dormant_until_discovered"].is<bool>())
            target.stationaryUntilDiscovered = source["dormant_until_discovered"].as<bool>();
        if (!cyberdeckConfigValid(target.deckConfig))
            return fail(ScenarioImportError::InvalidEnemyDefinition, "INVALID ENEMY LOADOUT", index);
        ++index;
    }
    output.setEnemyDefinitionCount(index);
    ScenarioImportResult result; result.success = true; return result;
}

ScenarioImportResult parse(JsonDocument& document, LoadedScenario& output, const BlackIceRegistry& registry,
                           const DemonRegistry& demonRegistry,
                           const EnemyNetrunnerRegistry& enemyRegistry)
{
    output.reset();
    if (!document.is<JsonObject>()) return fail(ScenarioImportError::MissingField, "ROOT OBJECT");
    JsonObjectConst root = document.as<JsonObjectConst>();

    if (root["schemaVersion"].is<int>()) {
        const int rawVersion = root["schemaVersion"].as<int>();
        if (rawVersion >= 0 && rawVersion <= 255) output.setSchemaVersion(static_cast<uint8_t>(rawVersion));
    }
    const char* headerId = optionalText(root, "id");
    const char* headerName = optionalText(root, "name");
    if (headerId != nullptr && strlen(headerId) < LoadedScenario::ID_LENGTH)
        copyText(headerId, output.idBuffer(), LoadedScenario::ID_LENGTH);
    if (headerName != nullptr && strlen(headerName) < LoadedScenario::NAME_LENGTH)
        copyText(headerName, output.nameBuffer(), LoadedScenario::NAME_LENGTH);

    if (!root["format"].is<const char*>()) return fail(ScenarioImportError::MissingField, "MISSING FORMAT");
    if (strcmp(root["format"].as<const char*>(), "netrun-architecture") != 0)
        return fail(ScenarioImportError::WrongFormat, "WRONG FORMAT");
    if (!root["schemaVersion"].is<int>()) return fail(ScenarioImportError::MissingField, "MISSING SCHEMA VERSION");
    const int schemaVersion = root["schemaVersion"].as<int>();
    if (schemaVersion != 1)
        return fail(ScenarioImportError::UnsupportedVersion, "SCHEMA VERSION NOT SUPPORTED");

    const char* id = optionalText(root, "id");
    const char* name = optionalText(root, "name");
    if (id == nullptr || id[0] == '\0' || name == nullptr || name[0] == '\0')
        return fail(ScenarioImportError::MissingField, "MISSING ID OR NAME");
    if (!copyText(id, output.idBuffer(), LoadedScenario::ID_LENGTH) ||
        !copyText(name, output.nameBuffer(), LoadedScenario::NAME_LENGTH))
        return fail(ScenarioImportError::StringTooLong, "ID OR NAME TOO LONG");

    ArchitectureDefinition& definition = output.mutableDefinition();
    const char* description = optionalText(root, "description");
    if (description != nullptr) {
        if (!copyText(description, output.descriptionBuffer(), LoadedScenario::DESCRIPTION_LENGTH))
            return fail(ScenarioImportError::StringTooLong, "DESCRIPTION TOO LONG");
        definition.description = output.descriptionBuffer();
    }

    {
        const ScenarioImportResult enemyResult = parseEnemies(root, output, enemyRegistry);
        if (!enemyResult.success) return enemyResult;
    }
    {
        const ScenarioImportResult localIceResult = parseLocalBlackIce(root, output, registry);
        if (!localIceResult.success) return localIceResult;
        const ScenarioImportResult localDemonResult = parseLocalDemons(root, output, demonRegistry);
        if (!localDemonResult.success) return localDemonResult;
        if (root.containsKey("demon"))
        {
            const char* demonId = optionalText(root, "demon");
            const DemonDefinition* resolved = resolveDemonDefinition(output, demonRegistry, demonId);
            if (resolved == nullptr) return fail(ScenarioImportError::UnknownDemon, "UNKNOWN DEMON");
            output.mutableDefinition().demon = DemonType::None;
            output.mutableDefinition().demonDefinition = resolved;
        }
    }

    if (!root["floors"].is<JsonArrayConst>()) return fail(ScenarioImportError::MissingField, "MISSING FLOORS");
    JsonArrayConst floors = root["floors"].as<JsonArrayConst>();
    if (floors.size() == 0) return fail(ScenarioImportError::InvalidValue, "NO FLOORS");
    if (floors.size() > MAX_ARCHITECTURE_FLOORS)
        return fail(ScenarioImportError::TooManyFloors, "TOO MANY FLOORS");

    bool usedIds[256] = {};
    size_t index = 0;
    for (JsonVariantConst item : floors)
    {
        if (!item.is<JsonObjectConst>()) return fail(ScenarioImportError::MissingField, "INVALID FLOOR", index);
        JsonObjectConst source = item.as<JsonObjectConst>();
        if (!source["id"].is<int>() || !source["type"].is<const char*>())
            return fail(ScenarioImportError::MissingField, "FLOOR ID OR TYPE", index);
        const int floorId = source["id"].as<int>();
        if (floorId < 0 || floorId > 255)
            return fail(ScenarioImportError::InvalidValue, "INVALID FLOOR ID", index, floorId);
        if (usedIds[floorId])
            return fail(ScenarioImportError::DuplicateFloorId, "DUPLICATE FLOOR ID", index, floorId);
        usedIds[floorId] = true;

        FloorDefinition& target = output.floors()[index];
        memset(&target, 0, sizeof(target));
        target.id = static_cast<uint8_t>(floorId);
        target.securityTier = SecurityTier::Low;
        target.blackIceType = BlackIceType::Ice01;
        target.blackIceCount = 0;
        LoadedScenario::FloorStrings& strings = output.strings(index);

        const char* contentId = optionalText(source, "content_id");
        const char* floorDescription = optionalText(source, "description");
        if (contentId != nullptr) {
            if (!copyText(contentId, strings.contentId, sizeof(strings.contentId)))
                return fail(ScenarioImportError::StringTooLong, "CONTENT ID TOO LONG", index, floorId);
            target.contentId = strings.contentId;
        }
        if (floorDescription != nullptr) {
            if (!copyText(floorDescription, strings.description, sizeof(strings.description)))
                return fail(ScenarioImportError::StringTooLong, "DESCRIPTION TOO LONG", index, floorId);
            target.description = strings.description;
        }

        const char* type = source["type"].as<const char*>();
        if (!strcmp(type, "password")) {
            target.type = FloorType::Password;
            if (!source["dv"].is<int>() || !source["security"].is<const char*>())
                return fail(ScenarioImportError::MissingField, "PASSWORD DATA", index, floorId);
            const char* security = source["security"].as<const char*>();
            if (!strcmp(security, "low")) target.securityTier = SecurityTier::Low;
            else if (!strcmp(security, "medium")) target.securityTier = SecurityTier::Medium;
            else if (!strcmp(security, "high")) target.securityTier = SecurityTier::High;
            else return fail(ScenarioImportError::InvalidSecurityTier, "INVALID SECURITY", index, floorId);
        } else if (!strcmp(type, "file")) {
            target.type = FloorType::File;
            if (!source["dv"].is<int>() || !source["file"].is<JsonObjectConst>())
                return fail(ScenarioImportError::MissingField, "FILE DATA", index, floorId);
            JsonObjectConst file = source["file"].as<JsonObjectConst>();
            const char* fileName = optionalText(file, "name");
            const char* fileType = optionalText(file, "type");
            if (fileName == nullptr || fileType == nullptr || !file["value"].is<uint32_t>())
                return fail(ScenarioImportError::InvalidValue, "INVALID FILE DATA", index, floorId);
            if (!copyText(fileName, strings.fileName, sizeof(strings.fileName)) ||
                !copyText(fileType, strings.fileType, sizeof(strings.fileType)))
                return fail(ScenarioImportError::StringTooLong, "FILE TEXT TOO LONG", index, floorId);
            target.fileName = strings.fileName; target.fileType = strings.fileType;
            target.fileValue = file["value"].as<uint32_t>();
        } else if (!strcmp(type, "black_ice")) {
            // Legacy Schema 1 spelling: normalize the ICE-only floor to the
            // canonical neutral content plus independent ICE placement.
            target.type = FloorType::Empty;
        } else if (!strcmp(type, "control")) {
            target.type = FloorType::ControlNode;
            if (!source["dv"].is<int>() || !source["control"].is<JsonObjectConst>())
                return fail(ScenarioImportError::MissingField, "CONTROL DATA", index, floorId);
            JsonObjectConst control = source["control"].as<JsonObjectConst>();
            const char* controlName = optionalText(control, "name");
            if (controlName == nullptr || controlName[0] == '\0')
                return fail(ScenarioImportError::MissingField, "CONTROL NAME", index, floorId);
            if (!copyText(controlName, strings.controlName, sizeof(strings.controlName)))
                return fail(ScenarioImportError::StringTooLong, "CONTROL NAME TOO LONG", index, floorId);
            target.controlName = strings.controlName;
            const char* controlDescription = optionalText(control, "description");
            if (controlDescription != nullptr) {
                if (!copyText(controlDescription, strings.controlDescription, sizeof(strings.controlDescription)))
                    return fail(ScenarioImportError::StringTooLong, "CONTROL TEXT TOO LONG", index, floorId);
                target.controlDescription = strings.controlDescription;
            }
        } else if (!strcmp(type, "empty")) {
            target.type = FloorType::Empty;
        } else return fail(ScenarioImportError::UnknownFloorType, "UNKNOWN FLOOR TYPE", index, floorId);

        // Hostile placement is independent of neutral floor content. Legacy
        // `type: black_ice` remains accepted above and requires this array.
        if (source.containsKey("ice") || !strcmp(type, "black_ice"))
        {
            if (!source["ice"].is<JsonArrayConst>())
                return fail(ScenarioImportError::MissingField, "ICE IDS", index, floorId);
            JsonArrayConst iceList = source["ice"].as<JsonArrayConst>();
            if (iceList.size() == 0) return fail(ScenarioImportError::InvalidValue, "NO BLACK ICE", index, floorId);
            if (iceList.size() > MAX_BLACK_ICE_PER_FLOOR)
                return fail(ScenarioImportError::TooManyBlackIce, "TOO MANY BLACK ICE", index, floorId);
            target.blackIceCount = static_cast<uint8_t>(iceList.size());
            for (size_t iceIndex = 0; iceIndex < iceList.size(); ++iceIndex)
            {
                JsonVariantConst rawId = iceList[iceIndex];
                if (!rawId.is<const char*>()) return fail(ScenarioImportError::UnknownBlackIceContent, "INVALID ICE ID", index, floorId);
                const BlackIceDefinition* ice = resolveBlackIceDefinition(output, registry, rawId.as<const char*>());
                if (ice == nullptr) return fail(ScenarioImportError::UnknownBlackIceContent, "UNKNOWN ICE ID", index, floorId);
                target.blackIceDefinitions[iceIndex] = ice;
                target.blackIceTypes[iceIndex] = ice->type;
            }
            target.blackIceDefinition = target.blackIceDefinitions[0];
            target.blackIceType = target.blackIceTypes[0];
        }
        if (source["dv"].is<int>()) {
            const int dv = source["dv"].as<int>();
            if (dv < 0 || dv > 255) return fail(ScenarioImportError::InvalidValue, "INVALID DV", index, floorId);
            target.difficulty = static_cast<uint8_t>(dv);
        }
        if (source.containsKey("enemy"))
        {
            const char* enemyId = optionalText(source, "enemy");
            const EnemyNetrunnerDefinition* enemy = resolveEnemyDefinition(output, enemyRegistry, enemyId);
            if (enemy == nullptr) return fail(ScenarioImportError::UnknownEnemy, "UNKNOWN ENEMY", index, floorId);
            for (size_t previous = 0; previous < index; ++previous)
                if (output.floors()[previous].enemyNetrunner == enemy)
                    return fail(ScenarioImportError::InvalidEnemyDefinition, "DUPLICATE ENEMY SPAWN", index, floorId);
            target.enemyNetrunner = enemy;
        }
        ++index;
    }
    definition.floorCount = index;
    // Schema 1 supports either a fully linear or a fully explicit graph.
    bool explicitGraph = false;
    for (JsonVariantConst item : floors)
        if (item.as<JsonObjectConst>().containsKey("next")) { explicitGraph = true; break; }
    if (explicitGraph)
    {
        size_t sourceIndex = 0;
        for (JsonVariantConst item : floors)
        {
            JsonObjectConst source = item.as<JsonObjectConst>();
            const int floorId = output.floors()[sourceIndex].id;
            if (!source["next"].is<JsonArrayConst>())
                return fail(ScenarioImportError::InvalidGraph, "ALL FLOORS NEED NEXT", sourceIndex, floorId);
            JsonArrayConst next = source["next"].as<JsonArrayConst>();
            if (next.size() > MAX_CONNECTIONS_PER_FLOOR)
                return fail(ScenarioImportError::InvalidGraph, "TOO MANY CONNECTIONS", sourceIndex, floorId);
            FloorDefinition& target = output.floors()[sourceIndex];
            for (JsonVariantConst rawTarget : next)
            {
                if (!rawTarget.is<int>())
                    return fail(ScenarioImportError::InvalidGraph, "INVALID CONNECTION", sourceIndex, floorId);
                const int targetId = rawTarget.as<int>();
                if (targetId < 0 || targetId > 255)
                    return fail(ScenarioImportError::UnknownConnection, "UNKNOWN CONNECTION", sourceIndex, targetId);
                if (targetId == floorId)
                    return fail(ScenarioImportError::SelfConnection, "SELF CONNECTION", sourceIndex, floorId);
                bool exists = false;
                for (size_t candidate = 0; candidate < definition.floorCount; ++candidate)
                    if (output.floors()[candidate].id == targetId) { exists = true; break; }
                if (!exists)
                    return fail(ScenarioImportError::UnknownConnection, "UNKNOWN CONNECTION", sourceIndex, targetId);
                for (size_t previous = 0; previous < target.connectionCount; ++previous)
                    if (target.connections[previous] == targetId)
                        return fail(ScenarioImportError::DuplicateConnection, "DUPLICATE CONNECTION", sourceIndex, floorId);
                target.connections[target.connectionCount++] = static_cast<uint8_t>(targetId);
            }
            ++sourceIndex;
        }
    }
    ScenarioImportResult result; result.success = true; return result;
}
}

const char* scenarioErrorMessage(ScenarioImportError error)
{
    switch (error) {
        case ScenarioImportError::None: return "OK";
        case ScenarioImportError::SdUnavailable: return "SD UNAVAILABLE";
        case ScenarioImportError::DirectoryMissing: return "NO SCENARIO DIR";
        case ScenarioImportError::FileOpenFailed: return "FILE OPEN FAILED";
        case ScenarioImportError::JsonParseFailed: return "INVALID JSON";
        case ScenarioImportError::WrongFormat: return "WRONG FORMAT";
        case ScenarioImportError::UnsupportedVersion: return "SCHEMA VERSION NOT SUPPORTED";
        case ScenarioImportError::MissingField: return "MISSING FIELD";
        case ScenarioImportError::TooManyFloors: return "TOO MANY FLOORS";
        case ScenarioImportError::DuplicateFloorId: return "DUPLICATE FLOOR";
        case ScenarioImportError::TooManyBlackIce: return "TOO MANY BLACK ICE";
        case ScenarioImportError::UnknownFloorType: return "UNKNOWN FLOOR";
        case ScenarioImportError::InvalidSecurityTier: return "INVALID SECURITY";
        case ScenarioImportError::UnknownBlackIce: return "UNKNOWN ICE";
        case ScenarioImportError::InvalidValue: return "INVALID VALUE";
        case ScenarioImportError::StringTooLong: return "TEXT TOO LONG";
        case ScenarioImportError::DuplicateScenarioId: return "DUPLICATE ID";
        case ScenarioImportError::UnknownConnection: return "UNKNOWN CONNECTION";
        case ScenarioImportError::DuplicateConnection: return "DUPLICATE CONNECTION";
        case ScenarioImportError::SelfConnection: return "SELF CONNECTION";
        case ScenarioImportError::InvalidGraph: return "INVALID GRAPH";
        case ScenarioImportError::TooManyEnemies: return "TOO MANY ENEMIES";
        case ScenarioImportError::DuplicateEnemyId: return "DUPLICATE ENEMY ID";
        case ScenarioImportError::UnknownEnemy: return "UNKNOWN ENEMY";
        case ScenarioImportError::UnknownEnemyProgram: return "UNKNOWN ENEMY PROGRAM";
        case ScenarioImportError::InvalidEnemyDefinition: return "INVALID ENEMY";
        case ScenarioImportError::InvalidEnemyBehavior: return "INVALID ENEMY BEHAVIOR";
        case ScenarioImportError::EnemyIdConflict: return "ENEMY ID CONFLICT";
        case ScenarioImportError::DuplicateBlackIceId: return "DUPLICATE LOCAL ICE";
        case ScenarioImportError::BlackIceIdConflict: return "ICE ID CONFLICT";
        case ScenarioImportError::UnknownBlackIceContent: return "UNKNOWN ICE ID";
        case ScenarioImportError::InvalidBlackIceDefinition: return "INVALID LOCAL ICE";
        case ScenarioImportError::DuplicateDemonId: return "DUPLICATE LOCAL DEMON";
        case ScenarioImportError::DemonIdConflict: return "DEMON ID CONFLICT";
        case ScenarioImportError::UnknownDemon: return "UNKNOWN DEMON";
        case ScenarioImportError::InvalidDemonDefinition: return "INVALID DEMON";
        case ScenarioImportError::TooManyDemons: return "TOO MANY DEMONS";
    }
    return "IMPORT ERROR";
}

ScenarioImportResult ScenarioLoader::loadJson(const char* json, LoadedScenario& output) const
{
    if (json == nullptr) return fail(ScenarioImportError::JsonParseFailed, "NULL JSON");
    DynamicJsonDocument document(JSON_CAPACITY);
    const DeserializationError error = deserializeJson(document, json);
    if (error) return fail(ScenarioImportError::JsonParseFailed, "JSON PARSE FAILED");
    LoadedScenario& staging = scenarioImportStaging();
    const ScenarioImportResult result = parse(document, staging, registry_, demonRegistry_, enemyRegistry_);
    if (result.success) output.commitFrom(staging);
    return result;
}

ScenarioImportResult ScenarioLoader::loadStream(Stream& stream, LoadedScenario& output) const
{
    DynamicJsonDocument document(JSON_CAPACITY);
    const DeserializationError error = deserializeJson(document, stream);
    if (error) return fail(ScenarioImportError::JsonParseFailed, "JSON PARSE FAILED");
    LoadedScenario& staging = scenarioImportStaging();
    const ScenarioImportResult result = parse(document, staging, registry_, demonRegistry_, enemyRegistry_);
    if (result.success) output.commitFrom(staging);
    return result;
}

ScenarioImportResult ScenarioLoader::loadFile(fs::FS& fs, const char* path, LoadedScenario& output) const
{
    File file = fs.open(path, FILE_READ);
    if (!file) return fail(ScenarioImportError::FileOpenFailed, "FILE OPEN FAILED");
    ScenarioImportResult result = loadStream(file, output);
    file.close();
    return result;
}
