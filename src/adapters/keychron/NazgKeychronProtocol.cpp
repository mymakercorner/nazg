// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeychronProtocol.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "adapters/NazgByteOrder.h"
#include "adapters/NazgProtocolError.h"

namespace nazg::keychron
{
    namespace
    {
        // What the firmware leaves as sent: the command id for 0xA0-0xA3, the id and the sub-id
        // for the groups.
        constexpr size_t c_EchoedTopLevel = 1;
        constexpr size_t c_EchoedGroup    = 2;

        // 0xA9 0x12 copies the bytes from byte 6 of the reply, and takes at most 26: past that the
        // firmware overflows the 32-byte report.
        constexpr size_t  c_ProfileChunkAt  = 6;
        constexpr uint8_t c_ProfileChunkMax = 26;

        // AMC_SET_TRAVAL names its keys from byte 8, three bytes -- 24 columns -- a row.
        constexpr size_t c_TravelRowsAt     = 8;
        constexpr size_t c_TravelColumnsMax = 24;

        // One analog_key_config_t, one okmc_config_t, one socd_config_t.
        constexpr size_t c_KeyConfigSize = 4;
        constexpr size_t c_DksSlotSize   = 19;
        constexpr size_t c_SocdSlotSize  = 3;
        constexpr size_t c_NameSize      = 30;

        // The published default curve, as fractions of full travel: 0, a quarter, three quarters,
        // all of it. DefaultActuationPoint() is the half.
        constexpr uint8_t c_SourceFullTravel = 40;

        [[nodiscard]] uint8_t StatusOf(const std::vector<uint8_t>& reply) noexcept { return reply[2]; }

        void RequireSuccess(const std::vector<uint8_t>& reply, const char* what)
        {
            if (StatusOf(reply) != 0)
                throw ProtocolError(std::string("Keychron: ") + what + " failed, status " +
                                    std::to_string(StatusOf(reply)));
        }
    }

    bool ProfileGeometry::Fits(uint8_t rows, uint8_t columns) const noexcept
    {
        return profileSize != 0 && ProfileSizeFor(rows, columns, dksSlots, socdSlots) == profileSize;
    }

    bool KeyConfig::FollowsProfile() const noexcept
    {
        return mode == KeyMode::FollowProfile && actuationPoint == 0 && pressSensitivity == 0 &&
               releaseSensitivity == 0 && advancedMode == AdvancedMode::None && advancedData == 0;
    }

    bool DksSlot::IsEmpty() const noexcept
    {
        if (shallowPress || shallowRelease || deepPress || deepRelease)
            return false;
        for (size_t i = 0; i < 4; ++i)
            if (keycodes[i] || onShallowPress[i] || onShallowRelease[i] || onDeepPress[i] || onDeepRelease[i])
                return false;
        return true;
    }

    KeyConfig DecodeKeyConfig(uint32_t value) noexcept
    {
        KeyConfig config;
        config.mode               = static_cast<KeyMode>(value & 0x3u);
        config.actuationPoint     = static_cast<uint8_t>((value >> 2) & 0x3Fu);
        config.pressSensitivity   = static_cast<uint8_t>((value >> 8) & 0x3Fu);
        config.releaseSensitivity = static_cast<uint8_t>((value >> 14) & 0x3Fu);
        config.advancedMode       = static_cast<AdvancedMode>((value >> 20) & 0xFu);
        config.advancedData       = static_cast<uint8_t>((value >> 24) & 0xFFu);
        return config;
    }

    uint32_t EncodeKeyConfig(const KeyConfig& config) noexcept
    {
        return (static_cast<uint32_t>(config.mode) & 0x3u) |
               ((static_cast<uint32_t>(config.actuationPoint) & 0x3Fu) << 2) |
               ((static_cast<uint32_t>(config.pressSensitivity) & 0x3Fu) << 8) |
               ((static_cast<uint32_t>(config.releaseSensitivity) & 0x3Fu) << 14) |
               ((static_cast<uint32_t>(config.advancedMode) & 0xFu) << 20) |
               (static_cast<uint32_t>(config.advancedData) << 24);
    }

    std::vector<uint8_t> Protocol::MakeRequest(Command command, uint8_t sub,
                                               std::initializer_list<uint8_t> arguments)
    {
        std::vector<uint8_t> frame = MakeFrame(static_cast<uint8_t>(command), { sub });
        size_t               index = 2;
        for (uint8_t byte : arguments)
            frame[index++] = byte;
        return frame;
    }

    bool Protocol::IsRefusal(const std::vector<uint8_t>& request, const std::vector<uint8_t>& reply) noexcept
    {
        if (reply.size() < via::c_ReportSize)
            return true;
        if (reply[0] == static_cast<uint8_t>(via::Command::Unhandled))
            return true;

        // A 0xA9 sub-id the firmware does not know comes back as the request, echoed unchanged, as
        // Vial does. The groups that write a status always touch byte 2, so an untouched report
        // with the arguments still in it is the echo.
        return reply == request;
    }

    Task<std::vector<uint8_t>> Protocol::AskTopLevel(Command command)
    {
        co_return co_await SendFrame(MakeFrame(static_cast<uint8_t>(command), {}), false, c_EchoedTopLevel);
    }

    Task<std::vector<uint8_t>> Protocol::Ask(Command command, uint8_t sub,
                                             std::initializer_list<uint8_t> arguments)
    {
        co_return co_await SendFrame(MakeRequest(command, sub, arguments), true, c_EchoedGroup);
    }

    Task<std::optional<Identity>> Protocol::Probe()
    {
        std::vector<uint8_t> frame   = MakeFrame(static_cast<uint8_t>(Command::GetProtocolVersion), {});
        ReplyCheck           isReply = ReplyTo(frame, c_EchoedTopLevel);
        std::vector<uint8_t> request = frame;
        std::vector<uint8_t> reply   = co_await m_Channel.Request(std::move(frame), std::move(isReply));

        if (IsRefusal(request, reply))
            co_return std::nullopt;

        Identity identity;
        identity.protocolVersion = reply[1];
        identity.commandSet      = reply[3];   // 0 where the branch sends a short reply

        // The feature bits, asked through the channel rather than Ask(): 0xA0 has already settled
        // that this is a Keychron, so a board that then refuses 0xA2 is still one -- it is found
        // with no features rather than not found at all. Probing each group is what decides the
        // sections anyway, and the bits under-report.
        std::vector<uint8_t> featureFrame   = MakeFrame(static_cast<uint8_t>(Command::GetSupportFeature), {});
        ReplyCheck           isFeatureReply = ReplyTo(featureFrame, c_EchoedTopLevel);
        std::vector<uint8_t> featureRequest = featureFrame;
        std::vector<uint8_t> features =
            co_await m_Channel.Request(std::move(featureFrame), std::move(isFeatureReply));

        if (!IsRefusal(featureRequest, features))
        {
            identity.featureBits = features[2];
            identity.extraBits   = features[3];
        }
        co_return identity;
    }

    Task<Identity> Protocol::GetIdentity()
    {
        const std::vector<uint8_t> version  = co_await AskTopLevel(Command::GetProtocolVersion);
        const std::vector<uint8_t> features = co_await AskTopLevel(Command::GetSupportFeature);

        Identity identity;
        identity.protocolVersion = version[1];
        identity.commandSet      = version[3];
        identity.featureBits     = features[2];
        identity.extraBits       = features[3];
        co_return identity;
    }

    Task<std::string> Protocol::GetFirmwareVersion()
    {
        const std::vector<uint8_t> reply = co_await AskTopLevel(Command::GetFirmwareVersion);

        // ASCII from byte 1, NUL-terminated -- and not necessarily terminated inside the report.
        std::string version;
        for (size_t i = 1; i < reply.size() && reply[i] != 0; ++i)
            version.push_back(static_cast<char>(reply[i]));
        co_return version;
    }

    Task<uint8_t> Protocol::GetDefaultLayer()
    {
        co_return (co_await AskTopLevel(Command::GetDefaultLayer))[1];
    }

    Task<bool> Protocol::SupportsMisc(Misc sub)
    {
        std::vector<uint8_t> request = MakeRequest(Command::Misc, static_cast<uint8_t>(sub));
        ReplyCheck           isReply = ReplyTo(request, c_EchoedGroup);
        std::vector<uint8_t> frame   = request;
        co_return !IsRefusal(request, co_await m_Channel.Request(std::move(frame), std::move(isReply)));
    }

    Task<bool> Protocol::SupportsRgb()
    {
        std::vector<uint8_t> request = MakeRequest(Command::Rgb, 0x01);   // RGB_GET_PROTOCOL_VER
        ReplyCheck           isReply = ReplyTo(request, c_EchoedGroup);
        std::vector<uint8_t> frame   = request;
        co_return !IsRefusal(request, co_await m_Channel.Request(std::move(frame), std::move(isReply)));
    }

    Task<bool> Protocol::SupportsAnalogMatrix()
    {
        std::vector<uint8_t> request = MakeRequest(Command::AnalogMatrix,
                                                   static_cast<uint8_t>(Analog::GetVersion));
        ReplyCheck           isReply = ReplyTo(request, c_EchoedGroup);
        std::vector<uint8_t> frame   = request;
        co_return !IsRefusal(request, co_await m_Channel.Request(std::move(frame), std::move(isReply)));
    }

    Task<uint8_t> Protocol::GetAnalogVersion()
    {
        // No status byte: the version is byte 2.
        co_return (co_await Ask(Command::AnalogMatrix, static_cast<uint8_t>(Analog::GetVersion)))[2];
    }

    Task<ProfileGeometry> Protocol::GetProfileGeometry()
    {
        // No status byte here either.
        const std::vector<uint8_t> reply =
            co_await Ask(Command::AnalogMatrix, static_cast<uint8_t>(Analog::GetProfilesInfo));

        ProfileGeometry geometry;
        geometry.currentProfile = reply[2];
        geometry.profileCount   = reply[3];
        geometry.profileSize    = ReadLittleEndian16(reply, 4);
        geometry.dksSlots       = reply[6];
        geometry.socdSlots      = reply[7];

        if (geometry.profileCount == 0 || geometry.profileSize == 0)
            throw ProtocolError("Keychron: the analog matrix reports no profiles");
        if (geometry.currentProfile >= geometry.profileCount)
            throw ProtocolError("Keychron: the current profile (" + std::to_string(geometry.currentProfile) +
                                ") is not one of the " + std::to_string(geometry.profileCount) + " reported");
        co_return geometry;
    }

    Task<std::vector<uint8_t>> Protocol::GetProfileRaw(uint8_t profile, const ProfileGeometry& geometry)
    {
        if (profile >= geometry.profileCount)
            throw std::invalid_argument("Keychron: profile " + std::to_string(profile) + " of " +
                                        std::to_string(geometry.profileCount));

        // Every chunk asked as one series: 38 requests for a K10 HE's profile, which must not wait
        // a frame each (DeviceChannel::RequestSeries()).
        std::vector<std::vector<uint8_t>> frames;
        std::vector<uint8_t>              sizes;
        for (size_t offset = 0; offset < geometry.profileSize; offset += c_ProfileChunkMax)
        {
            const uint8_t size =
                static_cast<uint8_t>(std::min<size_t>(c_ProfileChunkMax, geometry.profileSize - offset));
            const uint16_t at = static_cast<uint16_t>(offset);
            frames.push_back(MakeRequest(Command::AnalogMatrix, static_cast<uint8_t>(Analog::GetProfileRaw),
                                         { profile, LowByte(at), HighByte(at), size }));
            sizes.push_back(size);
        }

        const std::vector<std::vector<uint8_t>> replies = co_await SendFrames(std::move(frames), c_EchoedGroup);

        std::vector<uint8_t> bytes;
        bytes.reserve(geometry.profileSize);
        for (size_t index = 0; index < replies.size(); ++index)
            bytes.insert(bytes.end(), replies[index].begin() + c_ProfileChunkAt,
                         replies[index].begin() + c_ProfileChunkAt + sizes[index]);
        co_return bytes;
    }

    Task<std::vector<CurvePoint>> Protocol::GetCurve()
    {
        // No status byte: four points from byte 2, x then y.
        const std::vector<uint8_t> reply =
            co_await Ask(Command::AnalogMatrix, static_cast<uint8_t>(Analog::GetCurve));

        std::vector<CurvePoint> curve(4);
        for (size_t i = 0; i < 4; ++i)
            curve[i] = CurvePoint{ reply[2 + 2 * i], reply[3 + 2 * i] };
        co_return curve;
    }

    Task<RealtimeTravel> Protocol::GetRealtimeTravel(uint8_t row, uint8_t column)
    {
        const std::vector<uint8_t> reply = co_await Ask(
            Command::AnalogMatrix, static_cast<uint8_t>(Analog::GetRealtimeTravel), { row, column });
        RequireSuccess(reply, "the live travel");

        RealtimeTravel travel;
        travel.row             = reply[3];
        travel.column          = reply[4];
        travel.travel          = reply[5];
        travel.travelSixtieths = reply[6];
        travel.rawAdc          = ReadLittleEndian16(reply, 7);
        travel.restValue       = ReadLittleEndian16(reply, 9);
        travel.bottomValue     = ReadLittleEndian16(reply, 11);
        travel.pressed         = reply[13] == 0x11;
        co_return travel;
    }

    Task<CalibratedValue> Protocol::GetCalibratedValue(uint8_t row, uint8_t column)
    {
        // Never send 0xFF for either: it is meant to report the matrix size and instead reads the
        // key at (255, 255), out of bounds.
        if (row == 0xFF || column == 0xFF)
            throw std::invalid_argument("Keychron: (255, 255) reads out of bounds in the firmware");

        const std::vector<uint8_t> reply = co_await Ask(
            Command::AnalogMatrix, static_cast<uint8_t>(Analog::GetCalibratedValue), { row, column });

        CalibratedValue value;
        value.status      = reply[4];
        value.restValue   = ReadLittleEndian16(reply, 5);
        value.bottomValue = ReadLittleEndian16(reply, 7);

        const uint32_t bits = ReadLittleEndian32(reply, 9);
        std::memcpy(&value.scaleFactor, &bits, sizeof(float));
        co_return value;
    }

    Task<void> Protocol::SelectProfile(uint8_t profile)
    {
        RequireSuccess(co_await Ask(Command::AnalogMatrix, static_cast<uint8_t>(Analog::SelectProfile), { profile }),
                       "selecting the profile");
    }

    Task<void> Protocol::SetKeysTravel(uint8_t profile, const KeyConfig& config, const std::vector<KeyPosition>& keys)
    {
        if (config.mode == KeyMode::FollowProfile)
            throw std::invalid_argument("Keychron: a key's travel is written with a mode of its own");

        // [2] profile, [3] mode, [4] actuation point, [5] press and [6] release sensitivity, [7] 0 for
        // keys rather than the profile's global, then from [8] three bytes a row, one bit a column.
        std::vector<uint8_t> request = MakeRequest(Command::AnalogMatrix, static_cast<uint8_t>(Analog::SetTravel),
                                                   { profile, static_cast<uint8_t>(config.mode), config.actuationPoint,
                                                     config.pressSensitivity, config.releaseSensitivity, 0 });
        for (const KeyPosition key : keys)
        {
            if (key.column >= c_TravelColumnsMax || c_TravelRowsAt + 3u * key.row + 2u >= via::c_ReportSize)
                throw std::invalid_argument("Keychron: key (" + std::to_string(key.row) + ", " +
                                            std::to_string(key.column) + ") is past what one write can name");
            request[c_TravelRowsAt + 3u * key.row + key.column / 8u] |= static_cast<uint8_t>(1u << (key.column % 8u));
        }

        RequireSuccess(co_await SendFrame(std::move(request), true, c_EchoedGroup), "writing the travel");
    }

    Task<void> Protocol::SaveProfile(uint8_t profile)
    {
        RequireSuccess(co_await Ask(Command::AnalogMatrix, static_cast<uint8_t>(Analog::SaveProfile), { profile }),
                       "saving the profile");
    }

    Task<void> Protocol::ResetProfile(uint8_t profile)
    {
        RequireSuccess(co_await Ask(Command::AnalogMatrix, static_cast<uint8_t>(Analog::ResetProfile), { profile }),
                       "resetting the profile");
    }

    KeyConfig EffectiveConfig(const Profile& profile, KeyPosition key, uint8_t columns)
    {
        const size_t index = static_cast<size_t>(key.row) * columns + key.column;
        if (key.column >= columns || index >= profile.keys.size())
            throw std::invalid_argument("Keychron: key (" + std::to_string(key.row) + ", " +
                                        std::to_string(key.column) + ") is outside the profile");

        const KeyConfig& own    = profile.keys[index];
        const KeyConfig& global = profile.global;

        KeyConfig config          = own;
        config.mode               = own.mode != KeyMode::FollowProfile ? own.mode : global.mode;
        config.actuationPoint     = own.actuationPoint ? own.actuationPoint : global.actuationPoint;
        config.pressSensitivity   = own.pressSensitivity ? own.pressSensitivity : global.pressSensitivity;
        config.releaseSensitivity = own.releaseSensitivity ? own.releaseSensitivity : global.releaseSensitivity;
        // A global word saying follow is no setting: the firmware repairs it to regular at power-up.
        if (config.mode == KeyMode::FollowProfile)
            config.mode = KeyMode::Regular;
        return config;
    }

    std::vector<TravelWrite> PlanTravelWrites(const Profile& profile, uint8_t columns,
                                              const std::vector<KeyPosition>& keys, const TravelChange& change)
    {
        std::vector<TravelWrite> writes;
        for (const KeyPosition key : keys)
        {
            const KeyConfig now = EffectiveConfig(profile, key, columns);

            // What the write carries: the four fields, nothing advanced.
            KeyConfig written;
            written.mode               = change.mode.value_or(now.mode);
            written.actuationPoint     = change.actuationPoint.value_or(now.actuationPoint);
            written.pressSensitivity   = change.pressSensitivity.value_or(now.pressSensitivity);
            written.releaseSensitivity = change.releaseSensitivity.value_or(now.releaseSensitivity);

            const auto same = std::find_if(writes.begin(), writes.end(),
                                           [&](const TravelWrite& write) { return write.config == written; });
            if (same != writes.end())
                same->keys.push_back(key);
            else
                writes.push_back({ written, { key } });
        }
        return writes;
    }

    void ApplyTravelWrite(Profile& profile, uint8_t columns, const TravelWrite& write)
    {
        for (const KeyPosition key : write.keys)
        {
            const size_t index = static_cast<size_t>(key.row) * columns + key.column;
            if (key.column >= columns || index >= profile.keys.size())
                continue;
            KeyConfig& own         = profile.keys[index];
            own.mode               = write.config.mode;
            own.actuationPoint     = write.config.actuationPoint;
            own.pressSensitivity   = write.config.pressSensitivity;
            own.releaseSensitivity = write.config.releaseSensitivity;
        }
    }

    Profile DecodeProfile(const std::vector<uint8_t>& raw, uint8_t rows, uint8_t columns,
                          uint8_t dksSlots, uint8_t socdSlots)
    {
        const uint32_t expected = ProfileSizeFor(rows, columns, dksSlots, socdSlots);
        if (raw.size() != expected)
            throw std::invalid_argument("Keychron: a profile of " + std::to_string(raw.size()) +
                                        " bytes where " + std::to_string(rows) + "x" + std::to_string(columns) +
                                        " with " + std::to_string(dksSlots) + " DKS and " +
                                        std::to_string(socdSlots) + " SOCD slots needs " +
                                        std::to_string(expected));

        Profile  profile;
        size_t   offset = 0;
        profile.global  = DecodeKeyConfig(ReadLittleEndian32(raw, offset));
        offset += c_KeyConfigSize;

        const size_t keyCount = static_cast<size_t>(rows) * columns;
        profile.keys.reserve(keyCount);
        for (size_t i = 0; i < keyCount; ++i)
            profile.keys.push_back(DecodeKeyConfig(ReadLittleEndian32(raw, offset + c_KeyConfigSize * i)));
        offset += c_KeyConfigSize * keyCount;

        profile.dksSlots.reserve(dksSlots);
        for (size_t i = 0; i < dksSlots; ++i)
        {
            const size_t at = offset + c_DksSlotSize * i;

            DksSlot slot;
            const uint32_t travel = static_cast<uint32_t>(raw[at]) |
                                    (static_cast<uint32_t>(raw[at + 1]) << 8) |
                                    (static_cast<uint32_t>(raw[at + 2]) << 16);
            slot.shallowPress   = static_cast<uint8_t>(travel & 0x3Fu);
            slot.shallowRelease = static_cast<uint8_t>((travel >> 6) & 0x3Fu);
            slot.deepPress      = static_cast<uint8_t>((travel >> 12) & 0x3Fu);
            slot.deepRelease    = static_cast<uint8_t>((travel >> 18) & 0x3Fu);

            for (size_t k = 0; k < 4; ++k)
            {
                slot.keycodes[k] = ReadLittleEndian16(raw, at + 3 + 2 * k);

                // Two bytes an action pair: low nibble shallow, high nibble deep.
                const uint8_t shallow     = raw[at + 11 + 2 * k];
                const uint8_t deep        = raw[at + 12 + 2 * k];
                slot.onShallowPress[k]    = static_cast<uint8_t>(shallow & 0x0Fu);
                slot.onShallowRelease[k]  = static_cast<uint8_t>(shallow >> 4);
                slot.onDeepPress[k]       = static_cast<uint8_t>(deep & 0x0Fu);
                slot.onDeepRelease[k]     = static_cast<uint8_t>(deep >> 4);
            }
            profile.dksSlots.push_back(slot);
        }
        offset += c_DksSlotSize * dksSlots;

        profile.socdSlots.reserve(socdSlots);
        for (size_t i = 0; i < socdSlots; ++i)
        {
            const size_t at = offset + c_SocdSlotSize * i;

            SocdSlot slot;
            slot.firstRow     = static_cast<uint8_t>(raw[at] & 0x07u);
            slot.firstColumn  = static_cast<uint8_t>(raw[at] >> 3);
            slot.secondRow    = static_cast<uint8_t>(raw[at + 1] & 0x07u);
            slot.secondColumn = static_cast<uint8_t>(raw[at + 1] >> 3);
            slot.type         = static_cast<SocdType>(raw[at + 2]);
            profile.socdSlots.push_back(slot);
        }
        offset += c_SocdSlotSize * socdSlots;

        for (size_t i = 0; i < c_NameSize && raw[offset + i] != 0; ++i)
            profile.name.push_back(static_cast<char>(raw[offset + i]));
        offset += c_NameSize;

        profile.crc16 = ReadLittleEndian16(raw, offset);
        return profile;
    }

    uint8_t EstimateFullTravel(const std::vector<CurvePoint>& curve) noexcept
    {
        if (curve.empty())
            return 0;

        // The last point is full travel by construction: the curve maps the whole travel to the
        // axis. A curve a user flattened could say less, which is why this is only an estimate.
        const uint8_t last = curve.back().x;
        return last > c_SourceFullTravel ? uint8_t{ 0 } : last;
    }

    uint8_t DefaultActuationPoint(uint8_t fullTravel) noexcept
    {
        return static_cast<uint8_t>(fullTravel / 2);
    }
}
