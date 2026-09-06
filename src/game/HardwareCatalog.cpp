#include "HardwareCatalog.h"

namespace
{
constexpr HardwareDefinition kHardware[] = {
    {HardwareId::BackupDrive, "Backup Drive", 2, false},
    {HardwareId::DnaLock, "DNA Lock", 2, true},
    {HardwareId::HardenedCircuitry, "Hardened Circuitry", 1, true},
    {HardwareId::InsulatedWiring, "Insulated Wiring", 1, false},
    {HardwareId::KrashBarrier, "KRASH Barrier", 2, false},
    {HardwareId::RangeUpgrade, "Range Upgrade", 1, true},
};
}

const HardwareDefinition* hardwareDefinition(HardwareId id)
{
    for (const HardwareDefinition& definition : kHardware)
        if (definition.id == id) return &definition;
    return nullptr;
}
