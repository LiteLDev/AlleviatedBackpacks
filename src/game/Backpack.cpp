#include "game/Backpack.h"

#include <vector>

namespace abp::game {

namespace {
constexpr std::string_view kOwnListener = "abp:backpack";

Backpack::HookBuilder& hookBuilder() {
    static Backpack::HookBuilder fn;
    return fn;
}

std::vector<Backpack::HookBuilder>& hookContributors() {
    static std::vector<Backpack::HookBuilder> v;
    return v;
}
} // namespace

Backpack::Backpack(core::BackpackData data) : mData(std::move(data)) {
    mData.inventory.addChangeListener(std::string(kOwnListener), [this](int) { markDirty(); });
}

void Backpack::markDirty() {
    mDirty = true;
    ++mRevision;
}

bool Backpack::hasCapability(std::string_view cap) const { return mCapabilities.find(cap) != mCapabilities.end(); }

void Backpack::rebuildHooks() {
    mData.inventory.clearHooksExcept(kOwnListener);
    mCapabilities.clear();
    if (auto& fn = hookBuilder()) fn(*this);
    for (auto const& c : hookContributors()) c(*this);
    ++mRevision;
}

void Backpack::setHookBuilder(HookBuilder fn) { hookBuilder() = std::move(fn); }

void Backpack::addHookContributor(HookBuilder fn) { hookContributors().push_back(std::move(fn)); }

void Backpack::clearTransient(std::string const& key) {
    auto prefix = key + ":";
    std::erase_if(mTransient, [&](auto const& kv) { return kv.first == key || kv.first.starts_with(prefix); });
}

} // namespace abp::game
