#pragma once
#include <stdint.h>
#include "NetTypes.h"

class Program
{
public:
    Program() = default;
    Program(ProgramType type, const char* name, uint8_t attack, uint8_t defense, uint8_t maxRez)
        : type_(type), name_(name), attack_(attack), defense_(defense), rez_(maxRez), maxRez_(maxRez) {}
    Program(ProgramId id, ProgramType type, const char* name, uint8_t attack, uint8_t defense,
            uint8_t maxRez, uint8_t slotCost, bool oncePerRun, bool exclusive)
        : id_(id), type_(type), name_(name), attack_(attack), defense_(defense), rez_(maxRez),
          maxRez_(maxRez), slotCost_(slotCost), oncePerRun_(oncePerRun), exclusive_(exclusive) {}

    ProgramId id() const { return id_; }
    ProgramType type() const { return type_; }
    const char* name() const { return name_; }
    uint8_t attack() const { return attack_; }
    uint8_t defense() const { return defense_; }
    uint8_t rez() const { return rez_; }
    uint8_t maxRez() const { return maxRez_; }
    ProgramStatus status() const { return status_; }
    bool activatedThisRound() const { return activatedThisRound_; }
    uint8_t slotCost() const { return slotCost_; }
    bool oncePerRun() const { return oncePerRun_; }
    bool exclusive() const { return exclusive_; }
    bool usedThisRun() const { return usedThisRun_; }
    bool usable() const { return status_ == ProgramStatus::Rezzed; }
    void setStatus(ProgramStatus status)
    {
        status_ = status;
        if (status == ProgramStatus::Derezzed || status == ProgramStatus::Destroyed)
            rez_ = 0;
    }
    void restoreRez() { rez_ = maxRez_; }
    // Reset runtime state for a fresh run without changing the program definition.
    void resetForRun()
    {
        status_ = ProgramStatus::Inactive;
        rez_ = maxRez_;
        activatedThisRound_ = false;
        usedThisRun_ = false;
    }
    void takeRezDamage(int damage, ProgramDamageMode mode)
    {
        if (status_ != ProgramStatus::Rezzed || damage <= 0) return;
        if (damage >= rez_)
        {
            rez_ = 0;
            status_ = mode == ProgramDamageMode::DestroyAtZero
                ? ProgramStatus::Destroyed : ProgramStatus::Derezzed;
            return;
        }
        rez_ -= static_cast<uint8_t>(damage);
    }
    void setActivatedThisRound(bool activated) { activatedThisRound_ = activated; }
    void markUsedThisRun() { usedThisRun_ = true; }
    void resetRoundFlags() { activatedThisRound_ = false; }

private:
    ProgramId id_ = ProgramId::None;
    ProgramType type_ = ProgramType::Booster;
    const char* name_ = "";
    uint8_t attack_ = 0;
    uint8_t defense_ = 0;
    uint8_t rez_ = 0;
    uint8_t maxRez_ = 0;
    ProgramStatus status_ = ProgramStatus::Inactive;
    bool activatedThisRound_ = false;
    uint8_t slotCost_ = 1;
    bool oncePerRun_ = false;
    bool exclusive_ = false;
    bool usedThisRun_ = false;
};
