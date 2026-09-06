#pragma once

#include "RunnerProfile.h"
#include "CyberdeckConfig.h"

// NVS owns only player configuration. Runtime HP, program REZ/state and turn
// state are deliberately reconstructed by GameState for every run.
class PlayerConfigStore
{
public:
    void load(RunnerProfile& profile, CyberdeckConfig& deck) const;
    bool save(const RunnerProfile& profile, const CyberdeckConfig& deck) const;
    bool resetDefaults(RunnerProfile& profile, CyberdeckConfig& deck) const;
};
