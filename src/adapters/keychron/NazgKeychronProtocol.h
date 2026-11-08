// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// keychron::Protocol - Keychron's own commands on top of VIA, the Hall-effect settings among them.
// Every Keychron board is a VIA board first, so these sit beside via::Protocol exactly as the
// Leyden Jar's do: the full wire format is in docs/research_material/keychron-commands.md.
//
// The ids 0xA0-0xAB are what QMK's via.c hands to via_command_kb(). They collide with neither VIA
// (0x01-0x15) nor Vial (0xFE), so any board can be asked, which is how a Keychron is found:
// Probe(). The report is VIA's 32 bytes with the buffer echoed back and fields overwritten in place:
//
//     [cmd] [sub-id] [status] [data ...]      the groups 0xA7, 0xA8, 0xA9
//     [cmd] [data ...]                        0xA0 - 0xA3
//
// Keychron's own scalars are LITTLE-endian -- the firmware memcpys its structures on a
// little-endian ARM core -- so the two orders meet in one report stream. VIA's fields stay
// big-endian. The one exception is the matrix state Keychron answers for VIA, which keeps VIA's
// big-endian rows and is read through via::Protocol as on any other board.
//
// 0xAA (wireless DFU) and 0xAB (factory test) are NOT here and must never be sent: on a board
// without wireless 0xAA falls through to 0xAB, which ignores it, so the firmware sends no reply at
// all and the client waits out its timeout. Nothing a configurator needs is behind either.
//
// Two traps this header exists to encode, both met on Rico's K10 HE (2026-11-08):
//
//   - THE FEATURE BITS UNDER-REPORT. 0xA2 and 0xA7 0x01 advertise less than the firmware answers:
//     that board reports no Keychron RGB and no misc features at all, yet 0xA8 and 0xA7 0x12 both
//     reply. Only 0xFF -- or, for a 0xA9 sub-id, the request echoed unchanged -- means absent. So
//     Features are a hint for what to probe first, never the answer: use Supports().
//
//   - FULL TRAVEL IS PER BOARD. The source's FULL_TRAVEL_UNIT 40 (4.0 mm) is not universal; the
//     K10 HE tops out at 35. Nothing reports it, so it is carried in ProfileGeometry and
//     estimated by EstimateFullTravel(), never hard-coded.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"

namespace nazg::keychron
{
    // The top-level ids. 0xAA and 0xAB are deliberately absent: see the header note.
    enum class Command : uint8_t
    {
        GetProtocolVersion = 0xA0,
        GetFirmwareVersion = 0xA1,
        GetSupportFeature  = 0xA2,
        GetDefaultLayer    = 0xA3,
        Misc               = 0xA7,
        Rgb                = 0xA8,
        AnalogMatrix       = 0xA9,
    };

    // 0xA9's sub-commands, the analog matrix (Hall effect). Only those Nazg reads so far.
    enum class Analog : uint8_t
    {
        GetVersion      = 0x01,
        GetProfilesInfo = 0x10,
        SelectProfile   = 0x11,
        GetProfileRaw   = 0x12,
        SetProfileName  = 0x13,
        SetTravel       = 0x14,   // AMC_SET_TRAVAL in the firmware
        SetAdvanceMode  = 0x15,
        SetSocd         = 0x16,
        ResetProfile    = 0x1E,
        SaveProfile     = 0x1F,
        GetCurve        = 0x20,
        SetCurve        = 0x21,
        GetGamepadMode  = 0x22,
        SetGamepadMode  = 0x23,
        GetRealtimeTravel = 0x30,
        Calibrate         = 0x40,
        GetCalibrateState = 0x41,
        GetCalibratedValue = 0x42,
    };

    // 0xA7's sub-commands Nazg reads.
    enum class Misc : uint8_t
    {
        GetProtocolVersion = 0x01,
        GetDebounce        = 0x05,
        GetNkro            = 0x12,
    };

    // The 0xA2 feature bits, byte [2]. They UNDER-REPORT -- see the header -- so they say what is
    // probably there, never what is not.
    enum class Feature : uint8_t
    {
        DefaultLayer    = 0,
        Bluetooth       = 1,
        TwoPointFourGhz = 2,
        AnalogMatrix    = 3,
        StateNotify     = 4,
        DynamicDebounce = 5,
        SnapClick       = 6,
        KeychronRgb     = 7,
    };

    // What 0xA0 and 0xA2 say about a board. `featureBits` is byte [2]; `extraBits` is byte [3],
    // which 2025q3 builds send as 0x03 and the K10 HE as 0x53 -- kept raw, undecoded.
    struct Identity
    {
        uint8_t protocolVersion = 0;   // 2 on every published branch, 4 on the K10 HE
        uint8_t commandSet      = 0;   // QMK_COMMAND_SET, 2025q3 on; 0 where the reply is short
        uint8_t featureBits     = 0;
        uint8_t extraBits       = 0;

        [[nodiscard]] bool Has(Feature feature) const noexcept
        {
            return (featureBits >> static_cast<uint8_t>(feature)) & 1u;
        }
    };

    // 0xA9 0x10. The profile store's shape, and the full travel the board works in.
    struct ProfileGeometry
    {
        uint8_t  currentProfile = 0;
        uint8_t  profileCount   = 0;
        uint16_t profileSize    = 0;   // bytes, as the firmware reports
        uint8_t  dksSlots       = 0;
        uint8_t  socdSlots      = 0;

        // The board's full travel in 0.1 mm. NOT reported by any command: EstimateFullTravel()
        // puts the curve's last x here, and a key read at the bottom would settle it. 0 when
        // unknown, which a travel control must treat as "do not offer a maximum".
        uint8_t fullTravel = 0;

        // Whether `profileSize` is the size of a profile for a matrix of `rows` x `columns` with
        // these slot counts. Check this BEFORE parsing anything: the definition and the firmware
        // disagreeing means nothing read can be placed.
        [[nodiscard]] bool Fits(uint8_t rows, uint8_t columns) const noexcept;
    };

    // The size of one analog_matrix_profile_t: 2 + 4(RC + 1) + 30 + 19D + 3S.
    [[nodiscard]] constexpr uint32_t ProfileSizeFor(uint8_t rows, uint8_t columns,
                                                    uint8_t dksSlots, uint8_t socdSlots) noexcept
    {
        return 2u + 4u * (static_cast<uint32_t>(rows) * columns + 1u) + 30u +
               19u * dksSlots + 3u * socdSlots;
    }

    // analog_key_config_t, 4 bytes, read as one little-endian u32: GCC on ARM fills the packed
    // bitfields from the least significant bit. Confirmed on a K10 HE for every field but the two
    // advanced ones, which were zero there (keychron-commands.md, "The 2026-11-08 read").
    enum class KeyMode : uint8_t
    {
        FollowProfile = 0,
        Regular       = 1,
        RapidTrigger  = 2,
    };

    // The STORED values, which are not the wire's operation numbers -- 0-3 on the wire for
    // AMC_SET_ADVANCE_MODE, these in the profile.
    enum class AdvancedMode : uint8_t
    {
        None    = 0,
        Dks     = 3,
        Gamepad = 4,
        Toggle  = 5,
    };

    struct KeyConfig
    {
        KeyMode      mode               = KeyMode::FollowProfile;
        uint8_t      actuationPoint     = 0;   // 0.1 mm; 0 follows the profile
        uint8_t      pressSensitivity   = 0;   // rapid trigger, 0.1 mm; 0 follows the profile
        uint8_t      releaseSensitivity = 0;   // 0.1 mm; 0 follows the profile
        AdvancedMode advancedMode       = AdvancedMode::None;
        uint8_t      advancedData       = 0;   // the DKS slot, or the gamepad axis or button

        bool operator==(const KeyConfig&) const = default;

        // Whether the key is left entirely to the profile: what every key of an untouched board is.
        [[nodiscard]] bool FollowsProfile() const noexcept;
    };

    [[nodiscard]] KeyConfig DecodeKeyConfig(uint32_t value) noexcept;
    [[nodiscard]] uint32_t  EncodeKeyConfig(const KeyConfig& config) noexcept;

    // okmc_config_t, one DKS slot, 19 bytes. An action is three bits played in order from bit 0,
    // each a report 1 ms apart: bit 0 release, bit 1 press, bit 2 release. Only basic keycodes and
    // modifiers act.
    struct DksSlot
    {
        uint8_t  shallowPress   = 0;   // 0.1 mm
        uint8_t  shallowRelease = 0;
        uint8_t  deepPress      = 0;
        uint8_t  deepRelease    = 0;
        uint16_t keycodes[4]    = { 0, 0, 0, 0 };

        // Per keycode, the action at each of the four points, low nibble first as stored.
        uint8_t onShallowPress[4]   = { 0, 0, 0, 0 };
        uint8_t onShallowRelease[4] = { 0, 0, 0, 0 };
        uint8_t onDeepPress[4]      = { 0, 0, 0, 0 };
        uint8_t onDeepRelease[4]    = { 0, 0, 0, 0 };

        bool operator==(const DksSlot&) const = default;

        // A slot no key refers to reads all zero. Finding a free one means reading every key:
        // clearing a key does not clear its slot.
        [[nodiscard]] bool IsEmpty() const noexcept;
    };

    // socd_config_t, 3 bytes. Rows above 7 and columns above 31 cannot be stored.
    enum class SocdType : uint8_t
    {
        Off            = 0,
        DeeperWins     = 1,   // both stay pressed once both are past 3.8 mm
        DeeperThenLast = 2,
        LastWins       = 3,
        FirstKeyWins   = 4,
        SecondKeyWins  = 5,
        Neither        = 6,
    };

    struct SocdSlot
    {
        uint8_t  firstRow     = 0;
        uint8_t  firstColumn  = 0;
        uint8_t  secondRow    = 0;
        uint8_t  secondColumn = 0;
        SocdType type         = SocdType::Off;

        bool operator==(const SocdSlot&) const = default;

        [[nodiscard]] bool IsEmpty() const noexcept { return type == SocdType::Off; }
    };

    // One decoded analog_matrix_profile_t.
    struct Profile
    {
        KeyConfig              global;    // mode is never FollowProfile here
        std::vector<KeyConfig> keys;      // row-major, rows x columns
        std::vector<DksSlot>   dksSlots;
        std::vector<SocdSlot>  socdSlots;
        std::string            name;      // up to 30 bytes, NUL-terminated
        uint16_t               crc16 = 0; // the firmware never computes it
    };

    // A key's matrix position.
    struct KeyPosition
    {
        uint8_t row    = 0;
        uint8_t column = 0;

        bool operator==(const KeyPosition&) const = default;
    };

    // What a key does, its zeroes filled from the profile's global word: mode Regular or
    // RapidTrigger, never FollowProfile. What a key is told apart by -- never whether its own word
    // is 0, which a board the Launcher touched never has (keychron-commands.md, "Rapid Trigger is
    // written per key"). The advanced mode is kept as the key has it.
    [[nodiscard]] KeyConfig EffectiveConfig(const Profile& profile, KeyPosition key, uint8_t columns);

    // A change asked of some keys: the fields set, the rest kept as each key has them.
    struct TravelChange
    {
        std::optional<KeyMode> mode;
        std::optional<uint8_t> actuationPoint;     // 0.1 mm
        std::optional<uint8_t> pressSensitivity;   // (Press) Trigger Distance
        std::optional<uint8_t> releaseSensitivity; // (Release) Reset Distance
    };

    // One AMC_SET_TRAVAL for keys: the settings, written explicitly, and the keys that take them.
    struct TravelWrite
    {
        KeyConfig                config;   // mode and the three distances; the advanced fields unused
        std::vector<KeyPosition> keys;
    };

    // The writes that apply `change` to `keys`: each key's effective settings with the change on
    // top, keys ending alike in one write -- a change of one field on keys that differ elsewhere
    // takes as many writes as the results differ. Every field is written explicitly, as Keychron's
    // Launcher does; none is left at 0 to follow the profile.
    [[nodiscard]] std::vector<TravelWrite> PlanTravelWrites(const Profile& profile, uint8_t columns,
                                                            const std::vector<KeyPosition>& keys,
                                                            const TravelChange& change);

    // What the firmware does with a write, applied to the profile Nazg keeps: the four fields set,
    // the advanced mode left alone.
    void ApplyTravelWrite(Profile& profile, uint8_t columns, const TravelWrite& write);

    // 0xA9 0x20 / 0x21: travel -> an analog axis. x is 0.1 mm, y is the output, 0-127.
    struct CurvePoint
    {
        uint8_t x = 0;
        uint8_t y = 0;

        bool operator==(const CurvePoint&) const = default;
    };

    using Curve = CurvePoint[4];

    // 0xA9 0x30, one key as the firmware sees it.
    struct RealtimeTravel
    {
        uint8_t  row            = 0;
        uint8_t  column         = 0;
        uint8_t  travel         = 0;   // 0.1 mm -- floor(travelSixtieths / 6), the value to show
        uint8_t  travelSixtieths = 0;  // 1/60 mm; reached 215 on a K10 HE where 6 x 35 is 210
        uint16_t rawAdc         = 0;
        uint16_t restValue      = 0;
        uint16_t bottomValue    = 0;
        bool     pressed        = false;   // byte [13]: 0x11 pressed, 0x10 not
    };

    // 0xA9 0x42, a key's calibration. An unpopulated matrix position answers rest 0 and bottom 0
    // with a nonsense scale factor, which is how a client tells there is no key there.
    struct CalibratedValue
    {
        uint8_t  status      = 0;
        uint16_t restValue   = 0;
        uint16_t bottomValue = 0;
        float    scaleFactor = 0.0f;

        // rest and bottom both 0: no key at that position.
        [[nodiscard]] bool IsAbsent() const noexcept { return restValue == 0 && bottomValue == 0; }
    };

    class Protocol : public via::Protocol
    {
    public:
        using via::Protocol::Protocol;

        // Whether the board is a Keychron: 0xA0's reply, or none when it answers 0xFF -- every
        // stock VIA and Vial board. Throws only what the channel throws.
        [[nodiscard]] Task<std::optional<Identity>> Probe();

        // Throws ProtocolError when refused, as every call below.
        [[nodiscard]] Task<Identity>    GetIdentity();
        [[nodiscard]] Task<std::string> GetFirmwareVersion();   // "v1.0.0 2026-07-17-17:42:44"
        [[nodiscard]] Task<uint8_t>     GetDefaultLayer();

        // Whether a group or sub-command answers at all. This, not the feature bits, is what
        // decides a section: 0xFF -- or a 0xA9 sub-id echoed unchanged -- is the only "absent".
        [[nodiscard]] Task<bool> SupportsMisc(Misc sub);
        [[nodiscard]] Task<bool> SupportsRgb();
        [[nodiscard]] Task<bool> SupportsAnalogMatrix();

        // 0xA9 0x01. The version byte, 0x04 on every branch read so far.
        [[nodiscard]] Task<uint8_t> GetAnalogVersion();

        // 0xA9 0x10, with `fullTravel` left 0: EstimateFullTravel() fills it.
        [[nodiscard]] Task<ProfileGeometry> GetProfileGeometry();

        // 0xA9 0x12, reading one profile whole in 26-byte requests. `geometry` bounds it: the
        // firmware checks offset + size against ALL the profiles, not one, so past a profile's end
        // the size underflows and it copies up to 255 bytes into a 32-byte report. Nazg never lets
        // it: it reads inside one profile, 26 bytes at a time. There is no status byte either, so
        // a refused read would come back as the request; the bounds are checked here instead.
        [[nodiscard]] Task<std::vector<uint8_t>> GetProfileRaw(uint8_t profile, const ProfileGeometry& geometry);

        // 0xA9 0x20. Also the only no-interaction estimate of a board's full travel.
        [[nodiscard]] Task<std::vector<CurvePoint>> GetCurve();

        // 0xA9 0x30 / 0x42.
        [[nodiscard]] Task<RealtimeTravel>  GetRealtimeTravel(uint8_t row, uint8_t column);
        [[nodiscard]] Task<CalibratedValue> GetCalibratedValue(uint8_t row, uint8_t column);

        // ---- writes ----
        //
        // NONE OF THESE HAS MET A BOARD YET: the layouts are the source's (keychron-commands.md,
        // "Actuation and rapid trigger"). Each throws ProtocolError when the firmware answers a
        // non-zero status.

        // 0xA9 0x11: the board types with `profile` from now on. Stored at once.
        [[nodiscard]] Task<void> SelectProfile(uint8_t profile);

        // 0xA9 0x14 for `keys` of `profile`: mode, actuation point and both sensitivities, in RAM --
        // the board uses them at once if the profile is current, and forgets them at power-up
        // unless SaveProfile() follows. The keys go as one bit per column of each row, so at most
        // 8 rows of 24 columns: std::invalid_argument past that, or for a mode of FollowProfile.
        [[nodiscard]] Task<void> SetKeysTravel(uint8_t profile, const KeyConfig& config,
                                               const std::vector<KeyPosition>& keys);

        // 0xA9 0x1F: stores `profile` as it is in RAM.
        [[nodiscard]] Task<void> SaveProfile(uint8_t profile);

        // 0xA9 0x1E: `profile` back to the board's defaults. Stored at once.
        [[nodiscard]] Task<void> ResetProfile(uint8_t profile);

    private:
        // A request for one of the grouped commands: the id, the sub-id, then the arguments from
        // byte 2. The firmware echoes the first two bytes, which is the reply check.
        [[nodiscard]] static std::vector<uint8_t> MakeRequest(Command command, uint8_t sub,
                                                              std::initializer_list<uint8_t> arguments = {});

        // Sends it and throws unless the reply is the group's. `echoed` is 1 for 0xA0-0xA3, 2 for
        // the groups.
        [[nodiscard]] Task<std::vector<uint8_t>> Ask(Command command, uint8_t sub,
                                                     std::initializer_list<uint8_t> arguments = {});
        [[nodiscard]] Task<std::vector<uint8_t>> AskTopLevel(Command command);

        // Whether `reply` says the firmware does not have it: 0xFF in byte 0, or -- for a 0xA9
        // sub-id it does not know -- the request echoed back unchanged, as Vial does.
        [[nodiscard]] static bool IsRefusal(const std::vector<uint8_t>& request,
                                            const std::vector<uint8_t>& reply) noexcept;
    };

    // Decodes a profile read by GetProfileRaw(). Throws std::invalid_argument when `raw` is not
    // ProfileSizeFor(rows, columns, ...) bytes: the caller checks ProfileGeometry::Fits() first,
    // and this is the backstop.
    [[nodiscard]] Profile DecodeProfile(const std::vector<uint8_t>& raw, uint8_t rows, uint8_t columns,
                                        uint8_t dksSlots, uint8_t socdSlots);

    // The board's full travel in 0.1 mm, guessed from its curve: the last point's x, which is 35 on
    // a K10 HE and 40 on a board built to the published source. The curve is user-editable, so this
    // is a hint -- the exact value needs a key held to the bottom (GetRealtimeTravel). 0 when the
    // curve says nothing usable, which means "offer no maximum".
    [[nodiscard]] uint8_t EstimateFullTravel(const std::vector<CurvePoint>& curve) noexcept;

    // The firmware repairs a profile's global settings at power-up against ITS full travel, so the
    // defaults are fractions of it, floored: half for the actuation point, and the curve's x at
    // 0, a quarter, three quarters and all of it. 2.0 mm and (0,0) (10,31) (30,95) (40,127) in the
    // published source are those fractions of 40.
    [[nodiscard]] uint8_t DefaultActuationPoint(uint8_t fullTravel) noexcept;
}
