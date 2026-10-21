#include "mod/Main.h"

#include "ll/api/mod/RegisterHelper.h"

#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

ABP_REGISTER_CONFIG("general", abp::GeneralConfig);

namespace abp {

ll::io::Logger& log() { return Mod::getInstance().getSelf().getLogger(); }

Mod& Mod::getInstance() {
    static Mod instance;
    return instance;
}

bool Mod::load() {
    // Flush every message so e2e harnesses (and people tailing logs) see them immediately.
    getSelf().getLogger().setFlushLevel(ll::io::LogLevel::Trace);
    config::detail::setConfigDir(getSelf().getConfigDir());
    config::loadAll();
    loadTranslations(getSelf().getModDir() / "lang");
    loadModules();
    return true;
}

bool Mod::enable() {
    installModules();
    mEnabled = true;
    log().info("AlleviatedBackpacks enabled");
    return true;
}

bool Mod::disable() {
    uninstallModules();
    mEnabled = false;
    return true;
}

} // namespace abp

LL_REGISTER_MOD(abp::Mod, abp::Mod::getInstance());
