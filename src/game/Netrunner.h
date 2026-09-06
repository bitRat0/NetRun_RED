#pragma once
#include <stdint.h>
#include <string.h>

class Netrunner
{
public:
    Netrunner() = default;
    Netrunner(const char* handle, uint8_t interfaceRank, uint8_t maxHp)
        : Netrunner(handle, interfaceRank, maxHp, netActionsForInterfaceRank(interfaceRank)) {}
    Netrunner(const char* handle, uint8_t interfaceRank, uint8_t maxHp, uint8_t maxNetActions)
        : interfaceRank_(interfaceRank), hp_(maxHp), maxHp_(maxHp),
          maxNetActions_(maxNetActions), remainingNetActions_(maxNetActions)
    {
        strncpy(handle_, handle != nullptr ? handle : "", sizeof(handle_) - 1);
        handle_[sizeof(handle_) - 1] = '\0';
    }

    const char* handle() const { return handle_; }
    uint8_t interfaceRank() const { return interfaceRank_; }
    uint8_t hp() const { return hp_; }
    uint8_t maxHp() const { return maxHp_; }
    uint8_t maxNetActions() const { return maxNetActions_; }
    uint8_t remainingNetActions() const { return remainingNetActions_; }
    uint8_t move() const { return move_; }
    uint8_t baseMove() const { return baseMove_; }
    uint8_t baseInt() const { return baseInt_; }
    uint8_t currentInt() const { return currentInt_; }
    uint8_t baseRef() const { return baseRef_; }
    uint8_t currentRef() const { return currentRef_; }
    uint8_t baseDex() const { return baseDex_; }
    uint8_t currentDex() const { return currentDex_; }
    bool slideUsedThisTurn() const { return slideUsedThisTurn_; }
    static uint8_t netActionsForInterfaceRank(uint8_t interfaceRank)
    {
        if (interfaceRank <= 3) return 2;
        if (interfaceRank <= 6) return 3;
        if (interfaceRank <= 9) return 4;
        return 5;
    }
    static uint8_t netActionsAfterPenalty(
        uint8_t baseActions, uint8_t penalty, uint8_t minimumActions)
    {
        if (baseActions <= minimumActions) return baseActions;
        const uint8_t reducible = baseActions - minimumActions;
        return penalty >= reducible ? minimumActions : baseActions - penalty;
    }
    void resetTurn()
    {
        remainingNetActions_ = maxNetActions_;
        slideUsedThisTurn_ = false;
    }
    void resetTurn(uint8_t availableNetActions)
    {
        remainingNetActions_ = availableNetActions > maxNetActions_
            ? maxNetActions_ : availableNetActions;
        slideUsedThisTurn_ = false;
    }
    void resetRunStatuses()
    {
        move_ = baseMove_;
        currentInt_ = baseInt_;
        currentRef_ = baseRef_;
        currentDex_ = baseDex_;
    }
    uint8_t reduceMove(uint8_t amount)
    {
        const uint8_t previous = move_;
        move_ = amount >= move_ ? 1 : static_cast<uint8_t>(move_ - amount);
        return previous;
    }
    uint8_t reduceInt(uint8_t amount)
    {
        currentInt_ = amount >= currentInt_ ? 1 : static_cast<uint8_t>(currentInt_ - amount);
        return currentInt_;
    }
    uint8_t reduceRef(uint8_t amount)
    {
        currentRef_ = amount >= currentRef_ ? 1 : static_cast<uint8_t>(currentRef_ - amount);
        return currentRef_;
    }
    uint8_t reduceDex(uint8_t amount)
    {
        currentDex_ = amount >= currentDex_ ? 1 : static_cast<uint8_t>(currentDex_ - amount);
        return currentDex_;
    }
    bool spendNetAction()
    {
        if (remainingNetActions_ == 0) return false;
        --remainingNetActions_;
        return true;
    }
    bool useSlideThisTurn()
    {
        if (slideUsedThisTurn_) return false;
        slideUsedThisTurn_ = true;
        return true;
    }
    void takeDamage(int damage)
    {
        if (damage <= 0) return;
        hp_ = damage >= hp_ ? 0 : hp_ - damage;
    }

private:
    char handle_[16] = {};
    uint8_t interfaceRank_ = 0;
    uint8_t hp_ = 0;
    uint8_t maxHp_ = 0;
    uint8_t maxNetActions_ = 0;
    uint8_t remainingNetActions_ = 0;
    uint8_t baseMove_ = 5;
    uint8_t move_ = 5;
    // Fixed simulator values for REDSHIFT until character creation exists.
    uint8_t baseInt_ = 6;
    uint8_t currentInt_ = 6;
    uint8_t baseRef_ = 6;
    uint8_t currentRef_ = 6;
    uint8_t baseDex_ = 6;
    uint8_t currentDex_ = 6;
    bool slideUsedThisTurn_ = false;
};
