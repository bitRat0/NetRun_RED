#include "EnemyNetrunnerRegistry.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

#include "game/ProgramCatalog.h"

namespace
{
bool copyText(const char* source, char* target, size_t capacity)
{
    if (source == nullptr || source[0] == '\0' || strlen(source) >= capacity) return false;
    strcpy(target, source);
    return true;
}

bool validId(const char* id)
{
    if (id == nullptr || id[0] == '\0' || strlen(id) >= EnemyNetrunnerRegistry::ID_LENGTH) return false;
    for (const char* cursor = id; *cursor; ++cursor)
        if (!(islower(static_cast<unsigned char>(*cursor)) || isdigit(static_cast<unsigned char>(*cursor)) ||
              *cursor == '_' || *cursor == '-')) return false;
    return true;
}

EnemyCatalogResult fail(EnemyCatalogError error, const char* message)
{
    EnemyCatalogResult result; result.error = error; result.message = message; return result;
}

bool parseProgram(const char* name, ProgramId& output)
{
    if (name == nullptr) return false;
    for (uint8_t raw = static_cast<uint8_t>(ProgramId::Sword);
         raw < static_cast<uint8_t>(ProgramId::Count); ++raw)
    {
        const ProgramDefinition* definition = programDefinition(static_cast<ProgramId>(raw));
        if (definition != nullptr && !strcmp(name, definition->name))
        {
            output = static_cast<ProgramId>(raw);
            return true;
        }
    }
    return false;
}

bool parseBehavior(const char* name, EnemyBehaviorId& output)
{
    if (name == nullptr || !strcmp(name, "baseline")) { output = EnemyBehaviorId::Baseline; return true; }
    if (!strcmp(name, "defensive")) { output = EnemyBehaviorId::Defensive; return true; }
    if (!strcmp(name, "sentry")) { output = EnemyBehaviorId::Sentry; return true; }
    return false;
}
}

EnemyNetrunnerRegistry& enemyNetrunnerRegistry()
{
    static EnemyNetrunnerRegistry registry;
    return registry;
}

void EnemyNetrunnerRegistry::clear()
{
    memset(custom_, 0, sizeof(custom_));
    count_ = 0;
}

EnemyCatalogResult EnemyNetrunnerRegistry::loadJson(const char* json)
{
    if (json == nullptr) return fail(EnemyCatalogError::Json, "NULL JSON");
    DynamicJsonDocument document(JSON_CAPACITY);
    if (deserializeJson(document, json)) return fail(EnemyCatalogError::Json, "INVALID JSON");
    return parse(document);
}

EnemyCatalogResult EnemyNetrunnerRegistry::loadFile(fs::FS& fs, const char* path)
{
    if (!fs.exists(path)) { EnemyCatalogResult result; result.success = true; result.error = EnemyCatalogError::Missing; result.message = "MISSING"; return result; }
    File file = fs.open(path, FILE_READ);
    if (!file) return fail(EnemyCatalogError::FileOpen, "FILE OPEN FAILED");
    DynamicJsonDocument document(JSON_CAPACITY);
    const DeserializationError error = deserializeJson(document, file); file.close();
    if (error) return fail(EnemyCatalogError::Json, "INVALID JSON");
    return parse(document);
}

EnemyCatalogResult EnemyNetrunnerRegistry::parse(JsonDocument& document)
{
    if (!document.is<JsonObject>()) return fail(EnemyCatalogError::Format, "ROOT OBJECT");
    JsonObjectConst root = document.as<JsonObjectConst>();
    if (!root["format"].is<const char*>() || strcmp(root["format"], "netrun-enemy-catalog"))
        return fail(EnemyCatalogError::Format, "WRONG FORMAT");
    if (!root["version"].is<int>() || root["version"].as<int>() != 1)
        return fail(EnemyCatalogError::Version, "WRONG VERSION");
    if (!root["enemies"].is<JsonArrayConst>()) return fail(EnemyCatalogError::Field, "MISSING ENEMIES");
    JsonArrayConst entries = root["enemies"].as<JsonArrayConst>();
    if (entries.size() > MAX_CUSTOM_DEFINITIONS) return fail(EnemyCatalogError::TooMany, "TOO MANY DEFINITIONS");

    EnemyNetrunnerRegistry staging;
    for (JsonVariantConst raw : entries)
    {
        if (!raw.is<JsonObjectConst>()) return fail(EnemyCatalogError::Field, "INVALID ENTRY");
        JsonObjectConst source = raw.as<JsonObjectConst>();
        const char* id = source["id"].is<const char*>() ? source["id"].as<const char*>() : nullptr;
        const char* handle = source["handle"].is<const char*>() ? source["handle"].as<const char*>() : nullptr;
        if (!validId(id) || handle == nullptr || handle[0] == '\0' || strlen(handle) >= NAME_LENGTH)
            return fail(EnemyCatalogError::Field, "INVALID ID OR HANDLE");
        if (enemyNetrunnerDefinitionById(id) != nullptr) return fail(EnemyCatalogError::Conflict, "BUILTIN CONFLICT");
        if (staging.findByStableId(id) != nullptr) return fail(EnemyCatalogError::Duplicate, "DUPLICATE ID");
        if (!source["interface"].is<int>() || !source["hp"].is<int>() || !source["actions"].is<int>() || !source["programs"].is<JsonArrayConst>())
            return fail(EnemyCatalogError::Field, "MISSING STATS OR PROGRAMS");
        const int interfaceRank = source["interface"].as<int>();
        const int hp = source["hp"].as<int>();
        const int actions = source["actions"].as<int>();
        JsonArrayConst programs = source["programs"].as<JsonArrayConst>();
        if (interfaceRank < 1 || interfaceRank > 10 || hp < 1 || hp > 255 || actions < 1 || actions > 5 || programs.size() > MAX_CYBERDECK_PROGRAMS)
            return fail(EnemyCatalogError::Bounds, "INVALID STATS");
        EnemyNetrunnerDefinition definition = {};
        definition.id = id; definition.name = handle; definition.interfaceRank = static_cast<uint8_t>(interfaceRank);
        definition.maxHp = static_cast<uint8_t>(hp); definition.netActions = static_cast<uint8_t>(actions);
        definition.deckConfig.quality = CyberdeckQuality::Standard;
        for (JsonVariantConst rawProgram : programs)
        {
            ProgramId program = ProgramId::None;
            if (!rawProgram.is<const char*>() || !parseProgram(rawProgram.as<const char*>(), program))
                return fail(EnemyCatalogError::Program, "UNKNOWN PROGRAM");
            definition.deckConfig.programs[definition.deckConfig.programCount++] = program;
        }
        definition.ai = EnemyAiArchetype::AntiPersonnel;
        if (!source["ai"].isNull() && (!source["ai"].is<const char*>() ||
            strcmp(source["ai"].as<const char*>(), "anti_personnel")))
            return fail(EnemyCatalogError::Field, "INVALID AI");
        definition.stationaryUntilDiscovered = source["dormant_until_discovered"].is<bool>() && source["dormant_until_discovered"].as<bool>();
        if (!source["dormant_until_discovered"].isNull() && !source["dormant_until_discovered"].is<bool>())
            return fail(EnemyCatalogError::Field, "INVALID DORMANCY");
        definition.behavior = EnemyBehaviorId::Baseline;
        if (!source["behavior"].isNull() && (!source["behavior"].is<const char*>() || !parseBehavior(source["behavior"].as<const char*>(), definition.behavior)))
            return fail(EnemyCatalogError::Behavior, "UNKNOWN BEHAVIOR");
        if (!cyberdeckConfigValid(definition.deckConfig) || !staging.add(definition))
            return fail(EnemyCatalogError::Field, "INVALID DEFINITION");
    }
    clear();
    for (size_t index = 0; index < staging.count_; ++index)
        if (!add(staging.custom_[index].definition)) return fail(EnemyCatalogError::Field, "COMMIT FAILED");
    EnemyCatalogResult result; result.success = true; result.message = "LOADED"; return result;
}

bool EnemyNetrunnerRegistry::add(const EnemyNetrunnerDefinition& source)
{
    if (count_ >= MAX_CUSTOM_DEFINITIONS || !validId(source.id) || source.name == nullptr ||
        source.name[0] == '\0' || strlen(source.name) >= NAME_LENGTH ||
        source.interfaceRank < 1 || source.interfaceRank > 10 || source.maxHp == 0 ||
        source.netActions < 1 || source.netActions > 5 || !cyberdeckConfigValid(source.deckConfig) ||
        !enemyBehaviorIdValid(source.behavior) ||
        enemyNetrunnerDefinitionById(source.id) != nullptr || findByStableId(source.id) != nullptr)
        return false;

    StoredDefinition& target = custom_[count_];
    if (!copyText(source.id, target.id, sizeof(target.id)) ||
        !copyText(source.name, target.name, sizeof(target.name))) return false;
    target.definition = source;
    target.definition.id = target.id;
    target.definition.name = target.name;
    ++count_;
    return true;
}

const EnemyNetrunnerDefinition* EnemyNetrunnerRegistry::findByStableId(const char* id) const
{
    if (id == nullptr || id[0] == '\0') return nullptr;
    for (size_t index = 0; index < count_; ++index)
        if (!strcmp(custom_[index].id, id)) return &custom_[index].definition;
    return nullptr;
}
