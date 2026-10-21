#pragma once
// AlleviatedBackpacks public API: energy (Forge-Energy-style "FE" units).
//
// Header-only and free of game headers, so any mod (and host-side unit tests) can include it.
// See docs/API.md. ABI rules: interfaces only ever get new virtual functions appended at the end;
// existing signatures never change.

#include <algorithm>
#include <climits>

namespace abp::api {

/// Something that stores energy (FE). Mirrors the NeoForge `IEnergyStorage` contract:
/// - `receive` / `extract` return how much was (or, with `simulate`, would be) moved, never more
///   than asked and never negative;
/// - `stored() <= capacity()` normally holds (a storage whose capacity shrank may be over-full: it
///   then accepts nothing);
/// - `canReceive()` / `canExtract()` false means the matching call always returns 0.
class IEnergyStorage {
public:
    virtual ~IEnergyStorage() = default;

    /// Adds up to `maxReceive` FE. Returns the amount accepted.
    virtual int receive(int maxReceive, bool simulate) = 0;
    /// Removes up to `maxExtract` FE. Returns the amount removed.
    virtual int extract(int maxExtract, bool simulate) = 0;
    [[nodiscard]] virtual int  stored() const     = 0;
    [[nodiscard]] virtual int  capacity() const   = 0;
    [[nodiscard]] virtual bool canReceive() const = 0;
    [[nodiscard]] virtual bool canExtract() const = 0;
};

/// A plain energy buffer with per-call transfer limits (like NeoForge's `EnergyStorage`).
/// Override `onChanged()` to persist the value after a non-simulated change.
class EnergyStorage : public IEnergyStorage {
public:
    explicit EnergyStorage(int capacity, int maxReceive = INT_MAX, int maxExtract = INT_MAX, int energy = 0)
    : mCapacity(std::max(0, capacity)),
      mMaxReceive(std::max(0, maxReceive)),
      mMaxExtract(std::max(0, maxExtract)),
      mEnergy(std::clamp(energy, 0, std::max(0, capacity))) {}

    int receive(int maxReceive, bool simulate) override {
        if (!canReceive() || maxReceive <= 0) return 0;
        int n = std::min(std::max(0, mCapacity - mEnergy), std::min(mMaxReceive, maxReceive));
        if (!simulate && n > 0) {
            mEnergy += n;
            onChanged();
        }
        return n;
    }

    int extract(int maxExtract, bool simulate) override {
        if (!canExtract() || maxExtract <= 0) return 0;
        int n = std::min(mEnergy, std::min(mMaxExtract, maxExtract));
        if (!simulate && n > 0) {
            mEnergy -= n;
            onChanged();
        }
        return n;
    }

    [[nodiscard]] int  stored() const override { return mEnergy; }
    [[nodiscard]] int  capacity() const override { return mCapacity; }
    [[nodiscard]] bool canReceive() const override { return mMaxReceive > 0; }
    [[nodiscard]] bool canExtract() const override { return mMaxExtract > 0; }

    /// Sets the stored energy directly (clamped to `[0, capacity]`), e.g. when loading.
    void setStored(int energy) { mEnergy = std::clamp(energy, 0, mCapacity); }
    [[nodiscard]] int maxReceive() const { return mMaxReceive; }
    [[nodiscard]] int maxExtract() const { return mMaxExtract; }

protected:
    /// Called after every non-simulated change of `stored()`.
    virtual void onChanged() {}

    int mCapacity;
    int mMaxReceive;
    int mMaxExtract;
    int mEnergy;
};

/// Moves up to `max` FE from `from` to `to` (both sides' limits respected). Returns the amount
/// moved (or that would move, with `simulate`).
inline int transferEnergy(IEnergyStorage& from, IEnergyStorage& to, int max, bool simulate = false) {
    if (max <= 0 || &from == &to || !from.canExtract() || !to.canReceive()) return 0;
    int offered  = from.extract(max, true);
    int accepted = offered > 0 ? to.receive(offered, true) : 0;
    if (accepted <= 0 || simulate) return std::max(0, accepted);
    int taken = from.extract(accepted, false);
    int put   = to.receive(taken, false);
    // A storage that accepted less than its simulation promised: hand the rest back (a source
    // that can't take it back loses the difference; well-behaved storages never get here).
    if (put < taken) from.receive(taken - put, false);
    return put;
}

} // namespace abp::api
