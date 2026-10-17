// xboxkrnl data exports (variables like KeTimeStampBundle), created in guest
// memory on demand.

#pragma once

#include <cstdint>

namespace h3::kernel
{
    // Guest address of an xboxkrnl data export (KeTimeStampBundle,
    // VdHSIOCalibrationLock, ...) by ordinal, created on first use; one
    // that isn't implemented gets zeroed memory (logged). XenonRecomp only
    // resolves function imports; the module loader patches these into
    // variable import slots.
    uint32_t KernelVariable(uint16_t ordinal);
}
