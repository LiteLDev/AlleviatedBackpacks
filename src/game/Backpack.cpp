#include "game/Backpack.h"

namespace abp::game {

namespace {
Backpack::HookBuilder& hookBuilder() {
    static Backpack::HookBuilder fn;
    return fn;
}
} // namespace

Backpack::Backpack(core::BackpackData data) : mData(std::move(data)) {
    mData.inventory.addChangeListener("abp:backpack", [this](int) { markDirty(); });
}

void Backpack::markDirty() {
    mDirty = true;
    ++mRevision;
}

bool Backpack::hasCapability(std::string_view cap) const { return mCapabilities.find(cap) != mCapabilities.end(); }

void Backpack::rebuildHooks() {
    mData.inventory.clearFilters();
    mCapabilities.clear();
    if (auto& fn = hookBuilder()) fn(*this);
    ++mRevision;
}

void Backpack::setHookBuilder(HookBuilder fn) { hookBuilder() = std::move(fn); }

} // namespace abp::game
