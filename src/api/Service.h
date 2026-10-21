#pragma once
// Internal access to the capability service (the same object other mods get through
// `abp::api::capabilities()`, available even before it is registered with LeviLamina).

#include "api/abp/Capabilities.h"
#include "core/FluidRegistry.h"

namespace abp::api {

/// The service instance (lives as long as the DLL).
[[nodiscard]] CapabilityService& service();

/// The fluid registry behind the service (built-ins plus everything mods registered).
[[nodiscard]] core::FluidRegistry const& fluidRegistry();

} // namespace abp::api
