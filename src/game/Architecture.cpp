#include "Architecture.h"

bool Architecture::addConnection(uint8_t from, uint8_t to)
{
    Floor* source = floorAt(from);
    if (source == nullptr || to >= floorCount_ || from == to) return false;
    for (size_t index = 0; index < source->connectionCount; ++index)
        if (source->connections[index] == to) return true;
    if (source->connectionCount >= MAX_CONNECTIONS_PER_FLOOR) return false;
    size_t insert = source->connectionCount;
    while (insert > 0 && source->connections[insert - 1] > to)
    {
        source->connections[insert] = source->connections[insert - 1];
        --insert;
    }
    source->connections[insert] = to;
    ++source->connectionCount;
    return true;
}

bool Architecture::moveTo(uint8_t destination)
{
    if (destination >= floorCount_) return false;
    for (size_t index = 0; index < connectionCount(currentPosition_); ++index)
        if (connectionAt(currentPosition_, index) == destination)
        {
            previousPosition_ = static_cast<uint8_t>(currentPosition_);
            currentPosition_ = destination;
            discoverCurrentFloor();
            return true;
        }
    return false;
}

uint8_t Architecture::nextStepToward(uint8_t from, uint8_t target) const
{
    if (from >= floorCount_ || target >= floorCount_ || from == target) return NO_FLOOR;
    uint8_t queue[MAX_FLOORS] = {};
    uint8_t previous[MAX_FLOORS];
    bool seen[MAX_FLOORS] = {};
    for (size_t index = 0; index < MAX_FLOORS; ++index) previous[index] = NO_FLOOR;
    size_t read = 0, write = 0;
    queue[write++] = from; seen[from] = true;
    while (read < write)
    {
        const uint8_t current = queue[read++];
        for (size_t edge = 0; edge < connectionCount(current); ++edge)
        {
            const uint8_t next = connectionAt(current, edge);
            if (next >= floorCount_ || seen[next]) continue;
            seen[next] = true; previous[next] = current;
            if (next == target)
            {
                uint8_t step = target;
                while (previous[step] != from && previous[step] != NO_FLOOR) step = previous[step];
                return step;
            }
            if (write < MAX_FLOORS) queue[write++] = next;
        }
    }
    return NO_FLOOR;
}

uint8_t Architecture::canonicalParent(uint8_t floor) const
{
    if (floor == 0 || floor >= floorCount_) return NO_FLOOR;
    uint8_t queue[MAX_FLOORS] = {};
    uint8_t parent[MAX_FLOORS];
    for (size_t index = 0; index < MAX_FLOORS; ++index) parent[index] = NO_FLOOR;
    size_t read = 0, write = 0;
    queue[write++] = 0;
    parent[0] = 0;
    while (read < write)
    {
        const uint8_t current = queue[read++];
        for (size_t edge = 0; edge < connectionCount(current); ++edge)
        {
            const uint8_t next = connectionAt(current, edge);
            if (next >= floorCount_ || parent[next] != NO_FLOOR) continue;
            parent[next] = current;
            if (next == floor) return current;
            if (write < MAX_FLOORS) queue[write++] = next;
        }
    }
    return NO_FLOOR;
}

uint8_t Architecture::canonicalDepth(uint8_t floor) const
{
    if (floor >= floorCount_) return NO_FLOOR;
    if (floor == 0) return 0;
    uint8_t queue[MAX_FLOORS] = {};
    uint8_t depth[MAX_FLOORS];
    for (size_t index = 0; index < MAX_FLOORS; ++index) depth[index] = NO_FLOOR;
    size_t read = 0, write = 0;
    queue[write++] = 0; depth[0] = 0;
    while (read < write)
    {
        const uint8_t current = queue[read++];
        for (size_t edge = 0; edge < connectionCount(current); ++edge)
        {
            const uint8_t next = connectionAt(current, edge);
            if (next >= floorCount_ || depth[next] != NO_FLOOR) continue;
            depth[next] = static_cast<uint8_t>(depth[current] + 1);
            if (next == floor) return depth[next];
            if (write < MAX_FLOORS) queue[write++] = next;
        }
    }
    return NO_FLOOR;
}

uint8_t Architecture::maximumCanonicalDepth() const
{
    if (floorCount_ == 0) return NO_FLOOR;
    uint8_t queue[MAX_FLOORS] = {};
    uint8_t depth[MAX_FLOORS];
    for (size_t index = 0; index < MAX_FLOORS; ++index) depth[index] = NO_FLOOR;
    size_t read = 0, write = 0;
    uint8_t maximum = 0;
    queue[write++] = 0;
    depth[0] = 0;
    while (read < write)
    {
        const uint8_t current = queue[read++];
        if (depth[current] > maximum) maximum = depth[current];
        for (size_t edge = 0; edge < connectionCount(current); ++edge)
        {
            const uint8_t next = connectionAt(current, edge);
            if (next >= floorCount_ || depth[next] != NO_FLOOR) continue;
            depth[next] = static_cast<uint8_t>(depth[current] + 1);
            if (write < MAX_FLOORS) queue[write++] = next;
        }
    }
    return maximum;
}
