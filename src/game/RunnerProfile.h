#pragma once

#include <stdint.h>
#include <string.h>

struct RunnerProfile
{
    static constexpr size_t HANDLE_CAPACITY = 16;
    char handle[HANDLE_CAPACITY] = "M0u53";
    uint8_t interfaceRank = 4;
    uint8_t maxHp = 35;
};

inline RunnerProfile defaultRunnerProfile()
{
    return RunnerProfile();
}

inline bool runnerProfileValid(const RunnerProfile& profile)
{
    return profile.handle[0] != '\0' && memchr(profile.handle, '\0', RunnerProfile::HANDLE_CAPACITY) != nullptr &&
        profile.interfaceRank >= 1 && profile.interfaceRank <= 10 && profile.maxHp >= 1 && profile.maxHp <= 99;
}
