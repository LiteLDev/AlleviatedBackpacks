#pragma once

#include "ll/api/io/Logger.h"

namespace abp {

/// The mod's logger (LeviLamina console + log file, prefixed "AlleviatedBackpacks").
/// Every message is flushed immediately so e2e harnesses can tail the log.
[[nodiscard]] ll::io::Logger& log();

} // namespace abp
