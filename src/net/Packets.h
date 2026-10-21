#pragma once

// Companion protocol: LeviLamina runtime packets between the server mod and the client mod.
//
// Every packet carries the protocol version (u16) and a JSON body. The set is versioned by
// `core::screen::kProtocolVersion`; a peer drops packets of another version. Packets are only
// ever sent to a client that announced the companion (`HelloPacket`), so vanilla clients never
// see them; a server without the mod ignores the client's packets (LeviLamina drops unknown
// runtime packets; a vanilla BDS never receives them because the client only sends `Hello` and
// waits for `Welcome`).
//
// Directions:
//   client -> server: Hello, ScreenAction, KeyAction, ContentsRequest, RenderInfoRequest
//   server -> client: Welcome, ScreenOpen, ScreenUpdate, ScreenClose, Contents, RenderInfo
//
// Bodies (JSON):
//   Hello            {"version": "<mod version>", "features": [..]}
//   Welcome          {"version": "<mod version>", "features": [..]}
//   ScreenOpen       BackpackScreenModel::toJson()
//   ScreenUpdate     {"session": n, "patch": makePatch(...)}
//   ScreenClose      {"session": n}
//   ScreenAction     {"session": n, "action": "...", ...}            (see NativeScreens.h)
//   KeyAction        {"action": "...", ...}                          (see KeyActions.cpp)
//   ContentsRequest  {"uuid": "..."}
//   Contents         ContentsModel::toJson()
//   RenderInfoRequest{"uuid": "..."}
//   RenderInfo       RenderInfo::toJson()

#include <string>

#include "ll/api/network/packet/Packet.h"

#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"

#include "nlohmann/json.hpp"

#include "core/screen/ScreenModel.h"

namespace abp::net {

/// Largest JSON body accepted (a 120-slot screen model with NBT is far below this).
inline constexpr std::uint64_t kMaxBody = 4u * 1024u * 1024u;

/// A runtime packet with a protocol version and a JSON body.
template <class Derived>
class JsonPacket : public ll::network::PacketBase<Derived> {
public:
    int            version = core::screen::kProtocolVersion;
    nlohmann::json body    = nlohmann::json::object();

    JsonPacket() = default;
    explicit JsonPacket(nlohmann::json b) : body(std::move(b)) {}

    void write(BinaryStream& stream) const override {
        stream.writeUnsignedShort(static_cast<ushort>(version), "version", "");
        stream.writeString(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), "body", "");
    }

    [[nodiscard]] Bedrock::Result<void> read(ReadOnlyBinaryStream& stream) override {
        auto v = stream.getUnsignedShort();
        if (!v) return nonstd::make_unexpected(v.error());
        version = v.value();
        auto s  = stream.getString(kMaxBody);
        if (!s) return nonstd::make_unexpected(s.error());
        body = nlohmann::json::parse(s.value(), nullptr, false);
        return {};
    }

    /// Same protocol version and a JSON object body.
    [[nodiscard]] bool valid() const { return version == core::screen::kProtocolVersion && body.is_object(); }
};

// client -> server
class HelloPacket : public JsonPacket<HelloPacket> {
    using JsonPacket::JsonPacket;
};
class ScreenActionPacket : public JsonPacket<ScreenActionPacket> {
    using JsonPacket::JsonPacket;
};
class KeyActionPacket : public JsonPacket<KeyActionPacket> {
    using JsonPacket::JsonPacket;
};
class ContentsRequestPacket : public JsonPacket<ContentsRequestPacket> {
    using JsonPacket::JsonPacket;
};
class RenderInfoRequestPacket : public JsonPacket<RenderInfoRequestPacket> {
    using JsonPacket::JsonPacket;
};

// server -> client
class WelcomePacket : public JsonPacket<WelcomePacket> {
    using JsonPacket::JsonPacket;
};
class ScreenOpenPacket : public JsonPacket<ScreenOpenPacket> {
    using JsonPacket::JsonPacket;
};
class ScreenUpdatePacket : public JsonPacket<ScreenUpdatePacket> {
    using JsonPacket::JsonPacket;
};
class ScreenClosePacket : public JsonPacket<ScreenClosePacket> {
    using JsonPacket::JsonPacket;
};
class ContentsPacket : public JsonPacket<ContentsPacket> {
    using JsonPacket::JsonPacket;
};
class RenderInfoPacket : public JsonPacket<RenderInfoPacket> {
    using JsonPacket::JsonPacket;
};

/// Mod version string sent in the handshake.
[[nodiscard]] std::string modVersion();

} // namespace abp::net
