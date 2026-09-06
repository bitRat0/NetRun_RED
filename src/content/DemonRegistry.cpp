#include "DemonRegistry.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

namespace
{
bool copyText(const char* source, char* target, size_t capacity)
{
    if (source == nullptr || strlen(source) >= capacity) return false;
    strcpy(target, source);
    return true;
}

DemonCatalogResult fail(DemonCatalogError error, const char* message)
{
    DemonCatalogResult result; result.error = error; result.message = message; return result;
}

bool parseAnimation(const char* value, HostileAttackStyle& result) { if (value == nullptr) return false; if (!strcmp(value, "lunge")) result = HostileAttackStyle::Lunge; else if (!strcmp(value, "burst")) result = HostileAttackStyle::Burst; else if (!strcmp(value, "slash")) result = HostileAttackStyle::Slash; else if (!strcmp(value, "pulse")) result = HostileAttackStyle::Pulse; else return false; return true; }
bool validId(const char* id)
{
    if (id == nullptr || id[0] == '\0' || strlen(id) >= DemonInstance::MAX_STABLE_ID_LENGTH + 1) return false;
    for (const char* cursor = id; *cursor; ++cursor)
        if (!(islower(static_cast<unsigned char>(*cursor)) || isdigit(static_cast<unsigned char>(*cursor)) ||
              *cursor == '_' || *cursor == '-')) return false;
    return true;
}
}

DemonRegistry& demonRegistry()
{
    static DemonRegistry registry;
    return registry;
}

void DemonRegistry::clear()
{
    memset(custom_, 0, sizeof(custom_));
    count_ = 0;
}

DemonCatalogResult DemonRegistry::loadJson(const char* json)
{
    if (json == nullptr) return fail(DemonCatalogError::Json, "NULL JSON");
    DynamicJsonDocument document(JSON_CAPACITY);
    if (deserializeJson(document, json)) return fail(DemonCatalogError::Json, "INVALID JSON");
    return parse(document);
}

DemonCatalogResult DemonRegistry::loadFile(fs::FS& fs, const char* path)
{
    if (!fs.exists(path)) { DemonCatalogResult result; result.success = true; result.error = DemonCatalogError::Missing; result.message = "MISSING"; return result; }
    File file = fs.open(path, FILE_READ);
    if (!file) return fail(DemonCatalogError::FileOpen, "FILE OPEN FAILED");
    DynamicJsonDocument document(JSON_CAPACITY);
    const DeserializationError error = deserializeJson(document, file); file.close();
    if (error) return fail(DemonCatalogError::Json, "INVALID JSON");
    return parse(document);
}

DemonCatalogResult DemonRegistry::parse(JsonDocument& document)
{
    if (!document.is<JsonObject>()) return fail(DemonCatalogError::Format, "ROOT OBJECT");
    JsonObjectConst root = document.as<JsonObjectConst>();
    if (!root["format"].is<const char*>() || strcmp(root["format"], "netrun-demon-catalog"))
        return fail(DemonCatalogError::Format, "WRONG FORMAT");
    if (!root["version"].is<int>() || root["version"].as<int>() != 1)
        return fail(DemonCatalogError::Version, "WRONG VERSION");
    if (!root["demons"].is<JsonArrayConst>()) return fail(DemonCatalogError::Field, "MISSING DEMONS");
    JsonArrayConst entries = root["demons"].as<JsonArrayConst>();
    if (entries.size() > MAX_CUSTOM_DEFINITIONS) return fail(DemonCatalogError::TooMany, "TOO MANY DEFINITIONS");

    DemonRegistry staging;
    for (JsonVariantConst raw : entries)
    {
        if (!raw.is<JsonObjectConst>()) return fail(DemonCatalogError::Field, "INVALID ENTRY");
        JsonObjectConst source = raw.as<JsonObjectConst>();
        const char* id = source["id"].is<const char*>() ? source["id"].as<const char*>() : nullptr;
        const char* name = source["name"].is<const char*>() ? source["name"].as<const char*>() : nullptr;
        if (!validId(id) || name == nullptr || name[0] == '\0' || strlen(name) >= DemonInstance::MAX_DISPLAY_NAME_LENGTH + 1)
            return fail(DemonCatalogError::Field, "INVALID ID OR NAME");
        if (demonDefinitionByStableId(id) != nullptr) return fail(DemonCatalogError::Conflict, "BUILTIN CONFLICT");
        if (staging.findByStableId(id) != nullptr) return fail(DemonCatalogError::Duplicate, "DUPLICATE ID");
        if (!source["interface"].is<int>() || !source["rez"].is<int>() || !source["actions"].is<int>())
            return fail(DemonCatalogError::Field, "MISSING STATS");
        const int interfaceRank = source["interface"].as<int>();
        const int rez = source["rez"].as<int>();
        const int actions = source["actions"].as<int>();
        if (interfaceRank < 1 || interfaceRank > 10 || rez < 1 || actions < 1 || actions > DemonInstance::MAX_ACTIONS)
            return fail(DemonCatalogError::Bounds, "INVALID STATS");
        DemonVisualId visual = DemonVisualId::Orb01; HostileAttackStyle animation = HostileAttackStyle::Pulse;
        if (!source["visual"].isNull() && (!source["visual"].is<const char*>() || !parseDemonVisualId(source["visual"].as<const char*>(), visual))) return fail(DemonCatalogError::Visual, "UNKNOWN VISUAL");
        if (!source["animation"].isNull() && (!source["animation"].is<const char*>() || !parseAnimation(source["animation"].as<const char*>(), animation))) return fail(DemonCatalogError::Animation, "UNKNOWN ANIMATION");
        const DemonDefinition definition = {id, name, DemonType::None, visual, animation, rez,
            static_cast<uint8_t>(interfaceRank), static_cast<uint8_t>(actions), 0};
        if (!staging.add(definition)) return fail(DemonCatalogError::Field, "INVALID DEFINITION");
    }
    clear();
    for (size_t index = 0; index < staging.count_; ++index)
        if (!add(staging.custom_[index].definition)) return fail(DemonCatalogError::Field, "COMMIT FAILED");
    DemonCatalogResult result; result.success = true; result.message = "LOADED"; return result;
}

bool DemonRegistry::add(const DemonDefinition& source)
{
    if (count_ >= MAX_CUSTOM_DEFINITIONS || source.stableId == nullptr || source.displayName == nullptr ||
        source.stableId[0] == '\0' || source.displayName[0] == '\0' ||
        source.interfaceRank < 1 || source.interfaceRank > 10 || source.maxRez < 1 ||
        source.netActions < 1 || source.netActions > DemonInstance::MAX_ACTIONS ||
        !copyText(source.stableId, custom_[count_].stableId, sizeof(custom_[count_].stableId)) ||
        !copyText(source.displayName, custom_[count_].displayName, sizeof(custom_[count_].displayName)) ||
        demonDefinitionByStableId(source.stableId) != nullptr || findByStableId(source.stableId) != nullptr)
        return false;
    custom_[count_].definition = source;
    custom_[count_].definition.stableId = custom_[count_].stableId;
    custom_[count_].definition.displayName = custom_[count_].displayName;
    ++count_;
    return true;
}

const DemonDefinition* DemonRegistry::findByStableId(const char* stableId) const
{
    if (stableId == nullptr || stableId[0] == '\0') return nullptr;
    for (size_t index = 0; index < count_; ++index)
        if (!strcmp(custom_[index].stableId, stableId)) return &custom_[index].definition;
    return nullptr;
}
