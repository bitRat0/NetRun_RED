#include "BlackIceRegistry.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

namespace {
BlackIceCatalogResult fail(BlackIceCatalogError error, const char* message)
{ BlackIceCatalogResult result; result.error = error; result.message = message; return result; }

bool copyText(const char* source, char* target, size_t capacity)
{ return source != nullptr && strlen(source) < capacity && (strcpy(target, source), true); }

bool validId(const char* id)
{
    if (id == nullptr || id[0] == '\0' || strlen(id) >= BlackIceRegistry::ID_LENGTH) return false;
    for (const char* cursor = id; *cursor; ++cursor)
        if (!(islower(static_cast<unsigned char>(*cursor)) || isdigit(static_cast<unsigned char>(*cursor)) || *cursor == '_' || *cursor == '-')) return false;
    return true;
}

bool parseBehavior(const char* value, BlackIceEffectType& result)
{
    if (value == nullptr) return false;
    if (!strcmp(value, "direct_damage")) result = BlackIceEffectType::DamageOnly;
    else if (!strcmp(value, "derezz_defender_damage")) result = BlackIceEffectType::DerezzRandomDefenderAndDamage;
    else if (!strcmp(value, "destroy_installed_program")) result = BlackIceEffectType::DestroyRandomInstalledProgram;
    else if (!strcmp(value, "navigation_lock_damage")) result = BlackIceEffectType::DamageAndNavigationLock;
    else if (!strcmp(value, "action_penalty_damage")) result = BlackIceEffectType::DamageAndNextTurnNetActionPenalty;
    else if (!strcmp(value, "program_damage")) result = BlackIceEffectType::ProgramDamageDestroyAtZero;
    else if (!strcmp(value, "apply_fire")) result = BlackIceEffectType::ApplyFire;
    else if (!strcmp(value, "move_penalty_damage")) result = BlackIceEffectType::DamageAndReduceRunnerMove;
    else if (!strcmp(value, "stat_penalty")) result = BlackIceEffectType::ReduceRunnerStatsByAmount;
    else if (!strcmp(value, "unsafe_jack_out_damage")) result = BlackIceEffectType::DamageAndUnsafeJackOut;
    else return false;
    return true;
}

bool parseVisual(const char* value, IceVisualId& result)
{
    static const char* const names[] = {"hound", "bird", "serpent", "octopus", "wraith", "hunter", "scorpion", "rat", "winged", "feline", "skull", "giant"};
    if (value == nullptr) return false;
    for (uint8_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (!strcmp(value, names[index])) { result = static_cast<IceVisualId>(index); return true; }
    return false;
}

bool parseAnimation(const char* value, HostileAttackStyle& result)
{
    if (value == nullptr) return false;
    if (!strcmp(value, "lunge")) result = HostileAttackStyle::Lunge;
    else if (!strcmp(value, "pulse")) result = HostileAttackStyle::Pulse;
    else if (!strcmp(value, "slash")) result = HostileAttackStyle::Slash;
    else if (!strcmp(value, "burst")) result = HostileAttackStyle::Burst;
    else return false;
    return true;
}

bool effectValid(const BlackIceEffect& effect)
{
    const uint8_t dice = effect.damageDice;
    switch (effect.type) {
        case BlackIceEffectType::DamageOnly: case BlackIceEffectType::DamageAndNavigationLock:
        case BlackIceEffectType::DamageAndUnsafeJackOut: return dice >= 1 && dice <= 6;
        case BlackIceEffectType::DerezzRandomDefenderAndDamage: return dice >= 1 && dice <= 6;
        case BlackIceEffectType::DamageAndNextTurnNetActionPenalty: return dice >= 1 && dice <= 6 && effect.netActionPenalty >= 1 && effect.netActionPenalty <= 3 && effect.minimumNetActions >= 1 && effect.minimumNetActions <= 5;
        case BlackIceEffectType::ProgramDamageDestroyAtZero: return dice >= 1 && dice <= 6;
        case BlackIceEffectType::DamageAndReduceRunnerMove: return dice >= 1 && dice <= 6 && effect.statusDice >= 1 && effect.statusDice <= 3;
        case BlackIceEffectType::ReduceRunnerStatsByAmount: return effect.statusDice >= 1 && effect.statusDice <= 3;
        case BlackIceEffectType::DestroyRandomInstalledProgram: return dice == 0;
        case BlackIceEffectType::ApplyFire: return dice <= 6;
        default: return false;
    }
}
}

BlackIceRegistry& blackIceRegistry() { static BlackIceRegistry registry; return registry; }
const BlackIceDefinition* findBlackIceByContentId(const char* id) { return blackIceRegistry().findByContentId(id); }

void BlackIceRegistry::clear() { customCount_ = 0; memset(custom_, 0, sizeof(custom_)); }

const BlackIceDefinition* BlackIceRegistry::findByContentId(const char* id) const
{
    if (id == nullptr || id[0] == '\0') return nullptr;
    const BlackIceDefinition* custom = findCustomByContentId(id);
    return custom != nullptr ? custom : blackIceDefinitionByStableId(id);
}

const BlackIceDefinition* BlackIceRegistry::findCustomByContentId(const char* id) const
{
    if (id == nullptr || id[0] == '\0') return nullptr;
    for (size_t index = 0; index < customCount_; ++index)
        if (!strcmp(custom_[index].id, id)) return &custom_[index].definition;
    return nullptr;
}

const BlackIceDefinition* BlackIceRegistry::definitionAt(size_t index) const
{
    const size_t builtins = blackIceDefinitionCount();
    if (index < builtins) return blackIceDefinitionAt(index);
    index -= builtins;
    return index < customCount_ ? &custom_[index].definition : nullptr;
}

BlackIceCatalogResult BlackIceRegistry::loadJson(const char* json)
{
    if (json == nullptr) return fail(BlackIceCatalogError::Json, "NULL JSON");
    DynamicJsonDocument document(JSON_CAPACITY);
    if (deserializeJson(document, json)) return fail(BlackIceCatalogError::Json, "INVALID JSON");
    return parse(document);
}

BlackIceCatalogResult BlackIceRegistry::loadFile(fs::FS& fs, const char* path)
{
    if (!fs.exists(path)) { BlackIceCatalogResult result; result.success = true; result.error = BlackIceCatalogError::Missing; result.message = "MISSING"; return result; }
    File file = fs.open(path, FILE_READ);
    if (!file) return fail(BlackIceCatalogError::FileOpen, "FILE OPEN FAILED");
    DynamicJsonDocument document(JSON_CAPACITY);
    const DeserializationError error = deserializeJson(document, file); file.close();
    if (error) return fail(BlackIceCatalogError::Json, "INVALID JSON");
    return parse(document);
}

BlackIceCatalogResult BlackIceRegistry::parse(ArduinoJson::JsonDocument& document)
{
    if (!document.is<JsonObject>()) return fail(BlackIceCatalogError::Format, "ROOT OBJECT");
    JsonObjectConst root = document.as<JsonObjectConst>();
    if (!root["format"].is<const char*>() || strcmp(root["format"], "netrun-black-ice-catalog")) return fail(BlackIceCatalogError::Format, "WRONG FORMAT");
    if (!root["version"].is<int>() || root["version"].as<int>() != 1) return fail(BlackIceCatalogError::Version, "WRONG VERSION");
    if (!root["black_ice"].is<JsonArrayConst>()) return fail(BlackIceCatalogError::Field, "MISSING BLACK ICE");
    JsonArrayConst entries = root["black_ice"].as<JsonArrayConst>();
    if (entries.size() > MAX_CUSTOM_DEFINITIONS) return fail(BlackIceCatalogError::TooMany, "TOO MANY DEFINITIONS");
    static BlackIceStoredDefinition staging[MAX_CUSTOM_DEFINITIONS];
    memset(staging, 0, sizeof(staging)); size_t count = 0;
    for (JsonVariantConst raw : entries) {
        if (!raw.is<JsonObjectConst>()) return fail(BlackIceCatalogError::Field, "INVALID ENTRY");
        JsonObjectConst source = raw.as<JsonObjectConst>();
        const BlackIceCatalogResult parsed = parseBlackIceDefinition(source, staging[count]);
        if (!parsed.success) return parsed;
        staging[count].definition.shortEffect1 = "CATALOG";
        const char* id = staging[count].id;
        if (blackIceDefinitionByStableId(id) != nullptr) return fail(BlackIceCatalogError::Conflict, "BUILTIN CONFLICT");
        for (size_t previous = 0; previous < count; ++previous) if (!strcmp(staging[previous].id, id)) return fail(BlackIceCatalogError::Duplicate, "DUPLICATE ID");
        ++count;
    }
    memcpy(custom_, staging, sizeof(staging)); customCount_ = count;
    for (size_t index = 0; index < customCount_; ++index) {
        custom_[index].definition.stableId = custom_[index].id;
        custom_[index].definition.displayName = custom_[index].name;
    }
    BlackIceCatalogResult result; result.success = true; result.message = "LOADED"; return result;
}

BlackIceCatalogResult parseBlackIceDefinition(JsonObjectConst source, BlackIceStoredDefinition& target)
{
    const char* id = source["id"].is<const char*>() ? source["id"].as<const char*>() : nullptr;
    const char* name = source["name"].is<const char*>() ? source["name"].as<const char*>() : nullptr;
    const char* behavior = source["behavior"].is<const char*>() ? source["behavior"].as<const char*>() : nullptr;
    const char* visual = source["visual"].is<const char*>() ? source["visual"].as<const char*>() : nullptr;
    const char* animation = source["animation"].is<const char*>() ? source["animation"].as<const char*>() : nullptr;
    if (!validId(id) || !copyText(name, target.name, sizeof(target.name)) || !source["player_usable"].is<bool>()) return fail(BlackIceCatalogError::Id, "INVALID ID OR NAME");
    if (!source["per"].is<int>() || !source["spd"].is<int>() || !source["atk"].is<int>() || !source["def"].is<int>() || !source["rez"].is<int>()) return fail(BlackIceCatalogError::Field, "MISSING STATS");
    const int per = source["per"], spd = source["spd"], atk = source["atk"], def = source["def"], rez = source["rez"];
    if (per < 1 || per > 10 || spd < 1 || spd > 10 || atk < 1 || atk > 10 || def < 1 || def > 10 || rez < 1 || rez > 99) return fail(BlackIceCatalogError::Bounds, "INVALID STATS");
    BlackIceEffectType effectType; IceVisualId visualId; HostileAttackStyle animationStyle;
    if (!parseBehavior(behavior, effectType)) return fail(BlackIceCatalogError::Behavior, "UNKNOWN BEHAVIOR");
    if (!parseVisual(visual, visualId)) return fail(BlackIceCatalogError::Visual, "UNKNOWN VISUAL");
    if (!parseAnimation(animation, animationStyle)) return fail(BlackIceCatalogError::Animation, "UNKNOWN ANIMATION");
    BlackIceEffect effect = {effectType, static_cast<uint8_t>(source["damage_dice"] | 0), static_cast<uint8_t>(source["net_action_penalty"] | 0), static_cast<uint8_t>(source["minimum_actions"] | 0), static_cast<uint8_t>(source["status_amount"] | 0), 0};
    if (!effectValid(effect)) return fail(BlackIceCatalogError::Bounds, "INVALID EFFECT");
    copyText(id, target.id, sizeof(target.id));
    target.definition = {target.id, target.name, rez, BlackIceType::Count,
        effectType == BlackIceEffectType::ProgramDamageDestroyAtZero ? BlackIceClass::AntiProgram : BlackIceClass::AntiPersonnel,
        static_cast<uint8_t>(per), static_cast<uint8_t>(spd), static_cast<uint8_t>(atk), static_cast<uint8_t>(def), effect,
        "CUSTOM ICE", "CUSTOM", nullptr, visualId, animationStyle, source["player_usable"].as<bool>()};
    BlackIceCatalogResult result; result.success = true; result.message = "PARSED"; return result;
}

const char* blackIceCatalogErrorMessage(BlackIceCatalogError error)
{
    switch (error) { case BlackIceCatalogError::None: return "OK"; case BlackIceCatalogError::Missing: return "MISSING"; case BlackIceCatalogError::Duplicate: return "DUPLICATE"; case BlackIceCatalogError::Conflict: return "CONFLICT"; case BlackIceCatalogError::Behavior: return "UNKNOWN BEHAVIOR"; case BlackIceCatalogError::Visual: return "UNKNOWN VISUAL"; case BlackIceCatalogError::Animation: return "UNKNOWN ANIMATION"; case BlackIceCatalogError::Bounds: return "BOUNDS"; default: return "INVALID"; }
}
