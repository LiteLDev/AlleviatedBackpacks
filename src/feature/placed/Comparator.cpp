// Comparator output of placed backpacks (SB BackpackBlock.hasAnalogOutputSignal /
// getAnalogOutputSignal, P2-11).
//
// Data-driven blocks have no comparator output, and BlockType's comparator virtuals are folded
// with every other "return 0 / false" function, so they can't be hooked. Instead the six
// placed-backpack BlockType objects get a private copy of their vtable with the two comparator
// slots replaced; no other block or function is affected. The slots are found from the virtual
// call thunks of the two member function pointers.

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/placed/PlacedBackpacks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::placed {

namespace {

constexpr int kVtableCopy = 512; ///< entries copied (BlockType's vtable is shorter)

/// vtable byte offset called by an MSVC/clang-cl virtual call thunk, or -1.
std::ptrdiff_t vcallOffset(void const* thunk) {
    auto const* p = static_cast<std::uint8_t const*>(thunk);
    for (int hops = 0; hops < 4 && p[0] == 0xE9; ++hops) { // incremental-link jump
        std::int32_t rel;
        std::memcpy(&rel, p + 1, 4);
        p = p + 5 + rel;
    }
    if (p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01) return -1; // mov rax, [rcx]
    p += 3;
    if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x00) return 0;   // mov rax, [rax]; jmp rax
    if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x40) return static_cast<std::int8_t>(p[3]); // mov rax, [rax+disp8]
    if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x80) {            // mov rax, [rax+disp32]
        std::int32_t d;
        std::memcpy(&d, p + 3, 4);
        return d;
    }
    if (p[0] == 0xFF && p[1] == 0x20) return 0;                    // jmp [rax]
    if (p[0] == 0xFF && p[1] == 0x60) return static_cast<std::int8_t>(p[2]); // jmp [rax+disp8]
    if (p[0] == 0xFF && p[1] == 0xA0) {                            // jmp [rax+disp32]
        std::int32_t d;
        std::memcpy(&d, p + 2, 4);
        return d;
    }
    return -1;
}

template <class F>
void const* addressOf(F f) {
    static_assert(sizeof(F) == sizeof(void*), "single inheritance member function pointer");
    void const* p;
    std::memcpy(&p, &f, sizeof p);
    return p;
}

bool readable(void const* p, std::size_t bytes) {
    MEMORY_BASIC_INFORMATION mbi{};
    auto const*              at  = static_cast<std::uint8_t const*>(p);
    auto const*              end = at + bytes;
    while (at < end) {
        if (!VirtualQuery(at, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        at = static_cast<std::uint8_t const*>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool hasSignal(BlockType const*) { return true; }

int signal(BlockType const*, BlockSource& region, BlockPos const& pos, Block const&, unsigned char) {
    try {
        return PlacedBackpacks::get().comparatorSignal(region, pos);
    } catch (...) {
        return 0;
    }
}

struct State {
    std::ptrdiff_t                       hasSlot = -1, getSlot = -1;
    std::unordered_map<void**, void**>   copies;  ///< original vtable -> patched vtable
    std::unordered_map<BlockType*, void**> patched; ///< object -> original vtable
    bool                                 broken = false;
};
State gState;

void patch() {
    if (gState.broken) return;
    if (gState.hasSlot < 0 || gState.getSlot < 0) {
        gState.hasSlot = vcallOffset(addressOf(&BlockType::hasComparatorSignal));
        gState.getSlot = vcallOffset(addressOf(&BlockType::getComparatorSignal));
        if (gState.hasSlot < 0 || gState.getSlot < 0 || gState.hasSlot % 8 || gState.getSlot % 8 || gState.hasSlot / 8 >= kVtableCopy
            || gState.getSlot / 8 >= kVtableCopy) {
            auto dump = [](void const* p) {
                std::string s;
                auto const* b = static_cast<std::uint8_t const*>(p);
                for (int i = 0; i < 16; ++i) s += fmt::format("{:02x} ", b[i]);
                return s;
            };
            log().error("comparator support unavailable (vtable slots not found: {} / {})", dump(addressOf(&BlockType::hasComparatorSignal)), dump(addressOf(&BlockType::getComparatorSignal)));
            gState.broken = true;
            return;
        }
    }
    for (auto const& id : blockIds()) {
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        if (!type) continue;
        auto* t   = const_cast<BlockType*>(type.as_ptr());
        auto  obj = reinterpret_cast<void***>(t);
        void** vt = *obj;
        bool   ours = false;
        for (auto const& [orig, copy] : gState.copies) ours = ours || copy == vt;
        if (ours) continue;
        auto it = gState.copies.find(vt);
        if (it == gState.copies.end()) {
            if (!readable(vt - 1, (kVtableCopy + 1) * sizeof(void*))) {
                log().error("comparator support unavailable (vtable not readable)");
                gState.broken = true;
                return;
            }
            auto* copy = new void*[kVtableCopy + 1]; // lives for the process (objects may outlive us)
            std::memcpy(copy, vt - 1, (kVtableCopy + 1) * sizeof(void*));
            copy[1 + gState.hasSlot / 8] = reinterpret_cast<void*>(&hasSignal);
            copy[1 + gState.getSlot / 8] = reinterpret_cast<void*>(&signal);
            it = gState.copies.emplace(vt, copy + 1).first;
        }
        gState.patched[t] = vt;
        *obj              = it->second;
    }
}

void unpatch() {
    for (auto const& [t, vt] : gState.patched) *reinterpret_cast<void***>(t) = vt;
    gState.patched.clear();
}

} // namespace

/// Patches the placed-backpack block types (on enable and on every world start: the client
/// registers data-driven blocks per world).
class ComparatorModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListener = bus.emplaceListener<ll::event::ServerStartedEvent>([](auto&) { patch(); });
        mStopping = bus.emplaceListener<ll::event::ServerStoppingEvent>([](auto&) { unpatch(); });
        patch();
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        ll::event::EventBus::getInstance().removeListener(mStopping);
        unpatch();
    }

private:
    ll::event::ListenerPtr mListener;
    ll::event::ListenerPtr mStopping;
};

} // namespace abp::placed

ABP_REGISTER_MODULE(abp::placed::ComparatorModule);
