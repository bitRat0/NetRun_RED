#pragma once
#include <stddef.h>
#include <string.h>
#include "Program.h"
#include "CyberdeckConfig.h"
#include "HardwareCatalog.h"

class Cyberdeck
{
public:
    static constexpr size_t SLOT_COUNT = MAX_CYBERDECK_PROGRAMS;
    Cyberdeck() = default;
    void clear(CyberdeckQuality quality = CyberdeckQuality::Standard)
    {
        programCount_ = 0;
        usedSlots_ = 0;
        hardwareCount_ = 0;
        backupCount_ = 0;
        quality_ = quality;
    }
    bool addProgram(const Program& program)
    {
        if (programCount_ >= SLOT_COUNT || program.slotCost() == 0 ||
            usedSlots_ + program.slotCost() > slotCapacity()) return false;
        programs_[programCount_++] = program;
        usedSlots_ = static_cast<uint8_t>(usedSlots_ + program.slotCost());
        return true;
    }
    size_t programCount() const { return programCount_; }
    CyberdeckQuality quality() const { return quality_; }
    uint8_t slotCapacity() const { return cyberdeckSlotCapacity(quality_); }
    uint8_t usedSlots() const { return usedSlots_; }
    bool addHardware(HardwareId id)
    {
        const HardwareDefinition* definition = hardwareDefinition(id);
        if (definition == nullptr || hardwareCount_ >= MAX_CYBERDECK_HARDWARE ||
            usedSlots_ + definition->slotCost > slotCapacity()) return false;
        hardware_[hardwareCount_++] = id;
        usedSlots_ = static_cast<uint8_t>(usedSlots_ + definition->slotCost);
        return true;
    }
    size_t hardwareCount() const { return hardwareCount_; }
    HardwareId hardwareAt(size_t index) const { return index < hardwareCount_ ? hardware_[index] : HardwareId::Count; }
    bool hasHardware(HardwareId id) const
    {
        for (size_t index = 0; index < hardwareCount_; ++index) if (hardware_[index] == id) return true;
        return false;
    }
    size_t backupCount() const { return backupCount_; }
    const Program* backupAt(size_t index) const { return index < backupCount_ ? &backup_[index] : nullptr; }
    bool destroyProgram(size_t index)
    {
        if (index >= programCount_) return false;
        Program& program = programs_[index];
        if (hasHardware(HardwareId::BackupDrive) && backupCount_ < SLOT_COUNT)
        {
            backup_[backupCount_++] = program;
            usedSlots_ = static_cast<uint8_t>(usedSlots_ - program.slotCost());
            for (size_t move = index + 1; move < programCount_; ++move) programs_[move - 1] = programs_[move];
            --programCount_;
            return true;
        }
        program.setStatus(ProgramStatus::Destroyed);
        return false;
    }
    Program* programAt(size_t index) { return index < programCount_ ? &programs_[index] : nullptr; }
    const Program* programAt(size_t index) const { return index < programCount_ ? &programs_[index] : nullptr; }
    Program* findUsableProgram(const char* name)
    {
        for (size_t index = 0; index < programCount_; ++index)
        {
            if (programs_[index].usable() && strcmp(programs_[index].name(), name) == 0)
                return &programs_[index];
        }
        return nullptr;
    }
    Program* findUsableProgram(ProgramId id)
    {
        for (size_t index = 0; index < programCount_; ++index)
            if (programs_[index].id() == id && programs_[index].usable()) return &programs_[index];
        return nullptr;
    }
    const Program* findUsableProgram(ProgramId id) const
    {
        for (size_t index = 0; index < programCount_; ++index)
            if (programs_[index].id() == id && programs_[index].usable()) return &programs_[index];
        return nullptr;
    }
    void resetRoundFlags()
    {
        for (size_t index = 0; index < programCount_; ++index) programs_[index].resetRoundFlags();
    }
    void resetForRun()
    {
        for (size_t index = 0; index < programCount_; ++index) programs_[index].resetForRun();
    }

private:
    Program programs_[SLOT_COUNT];
    Program backup_[SLOT_COUNT];
    HardwareId hardware_[MAX_CYBERDECK_HARDWARE] = {};
    size_t programCount_ = 0;
    uint8_t usedSlots_ = 0;
    size_t hardwareCount_ = 0;
    size_t backupCount_ = 0;
    CyberdeckQuality quality_ = CyberdeckQuality::Standard;
};
