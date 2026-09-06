#pragma once
#include <stddef.h>
#include "NetTypes.h"

class Architecture
{
public:
    static constexpr size_t MAX_FLOORS = MAX_ARCHITECTURE_FLOORS;
    static constexpr uint8_t NO_FLOOR = 0xff;
    explicit Architecture(const char* name = "") : name_(name) {}
    Architecture(const char* id, const char* name, const char* description)
        : id_(id), name_(name), description_(description) {}
    void initialize(const char* id, const char* name, const char* description)
    {
        id_ = id != nullptr ? id : "";
        name_ = name != nullptr ? name : "";
        description_ = description;
        floorCount_ = 0;
        currentPosition_ = 0;
        previousPosition_ = NO_FLOOR;
    }
    const char* id() const { return id_; }
    const char* name() const { return name_; }
    const char* description() const { return description_; }
    bool addFloor(const Floor& floor)
    {
        if (floorCount_ >= MAX_FLOORS) return false;
        floors_[floorCount_++] = floor;
        return true;
    }
    size_t floorCount() const { return floorCount_; }
    size_t currentPosition() const { return currentPosition_; }
    uint8_t previousPosition() const { return previousPosition_; }
    Floor* floorAt(size_t index) { return index < floorCount_ ? &floors_[index] : nullptr; }
    const Floor* floorAt(size_t index) const { return index < floorCount_ ? &floors_[index] : nullptr; }
    Floor* currentFloor() { return floorAt(currentPosition_); }
    const Floor* currentFloor() const { return floorAt(currentPosition_); }
    size_t connectionCount(size_t floor) const
    { const Floor* value = floorAt(floor); return value != nullptr ? value->connectionCount : 0; }
    uint8_t connectionAt(size_t floor, size_t connection) const
    { const Floor* value = floorAt(floor); return value != nullptr && connection < value->connectionCount ? value->connections[connection] : NO_FLOOR; }
    bool addConnection(uint8_t from, uint8_t to);
    bool moveTo(uint8_t destination);
    bool moveForward()
    {
        return currentPosition_ + 1 < floorCount_ && moveTo(static_cast<uint8_t>(currentPosition_ + 1));
    }
    bool moveBackward()
    {
        return currentPosition_ > 0 && moveTo(static_cast<uint8_t>(currentPosition_ - 1));
    }
    uint8_t nextStepToward(uint8_t from, uint8_t target) const;
    // Deterministic BFS parent rooted at floor zero. UI-only navigation labels
    // use this instead of movement history; it never changes graph rules.
    uint8_t canonicalParent(uint8_t floor) const;
    uint8_t canonicalDepth(uint8_t floor) const;
    // Largest BFS depth reachable from the entry floor. More than one floor
    // may share it and therefore qualify as a valid Architecture endpoint.
    uint8_t maximumCanonicalDepth() const;
    void reset()
    {
        currentPosition_ = 0;
        for (size_t index = 0; index < floorCount_; ++index)
        {
            floors_[index].discovered = false;
            floors_[index].visited = false;
            floors_[index].resolved = false;
            floors_[index].identified = false;
            floors_[index].controlled = false;
            floors_[index].controlOwner = ControlOwner::None;
            floors_[index].blackIceTriggered = false;
            floors_[index].downloaded = false;
        }
        previousPosition_ = NO_FLOOR;
    }
    void discoverCurrentFloor()
    {
        Floor* floor = currentFloor();
        if (floor != nullptr) { floor->discovered = true; floor->visited = true; }
    }

private:
    const char* id_ = "";
    const char* name_ = "";
    const char* description_ = nullptr;
    Floor floors_[MAX_FLOORS];
    size_t floorCount_ = 0;
    size_t currentPosition_ = 0;
    uint8_t previousPosition_ = NO_FLOOR;
};
