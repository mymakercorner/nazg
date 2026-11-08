// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins Keychron's own commands: the probe that finds a Keychron among VIA and Vial boards, the
// request's layout, the little-endian scalars, the profile read's chunking and its bounds, and
// the packed bitfields of analog_matrix_profile_t.
//
// THE BYTES HERE ARE A REAL BOARD'S. Every reply below was recorded from Rico's Keychron K10 HE
// ANSI (3434:0EA7, firmware v1.0.0 2026-07-17-17:42:44) on 2026-11-08 with a read-only probe, and
// the profile's global word was cross-checked against Keychron's own Launcher, which shows the
// 1.7 mm this test decodes. The layouts are in docs/research_material/keychron-commands.md,
// "The 2026-11-08 read".
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/keychron/NazgKeychronProtocol.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace keychron = nazg::keychron;

using nazg::ProtocolError;
using nazg::Task;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");

        return task.TakeResult();
    }

    template <typename TException, typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const TException&)
        {
            return true;
        }
        return false;
    }

    // The K10 HE's matrix and slot counts.
    constexpr uint8_t c_Rows = 6;
    constexpr uint8_t c_Cols = 21;
    constexpr uint8_t c_Dks  = 20;
    constexpr uint8_t c_Socd = 20;

    // ---- the board's own replies, byte for byte ----
    const std::vector<uint8_t> c_A0{ 0xA0, 0x04, 0x00, 0x02 };
    const std::vector<uint8_t> c_A2{ 0xA2, 0x00, 0x09, 0x53, 0x00, 0x15, 0x3F, 0xF1, 0x11 };
    const std::vector<uint8_t> c_A3{ 0xA3, 0x02, 0x00 };
    const std::vector<uint8_t> c_A9Version{ 0xA9, 0x01, 0x04, 0x00, 0x1E };
    const std::vector<uint8_t> c_A9Info{ 0xA9, 0x10, 0x00, 0x03, 0xD4, 0x03, 0x14, 0x14 };
    const std::vector<uint8_t> c_A9Curve{ 0xA9, 0x20, 0x00, 0x00, 0x08, 0x1F, 0x1A, 0x5F, 0x23, 0x7F };
    const std::vector<uint8_t> c_A8Version{ 0xA8, 0x01, 0x00, 0x01 };
    const std::vector<uint8_t> c_A7Nkro{ 0xA7, 0x12, 0x00, 0x03 };

    void TestProbe()
    {
        std::printf("the probe\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply(c_A0);
            channel.Reply(c_A2);

            const auto identity = Run(protocol.Probe());
            Check(identity.has_value(), "a Keychron answers 0xA0");
            Check(identity && identity->protocolVersion == 4, "the K10 HE reports protocol 4, not the source's 2");
            Check(identity && identity->commandSet == 2, "the command set is byte 3");
            Check(identity && identity->featureBits == 0x09, "the feature bits are byte 2 of 0xA2");
            Check(identity && identity->extraBits == 0x53, "byte 3 is kept raw: 0x53, not the source's 0x03");

            const std::vector<uint8_t>& request = channel.RequestAt(0);
            Check(request.size() == via::c_ReportSize && request[0] == 0xA0, "asked with 0xA0, one report");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.ReplyUnhandled();
            Check(!Run(protocol.Probe()).has_value(), "a stock VIA or Vial board answers 0xFF: not a Keychron");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.ReplyEcho();
            Check(!Run(protocol.Probe()).has_value(), "a firmware echoing the request is not a Keychron either");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply(c_A0);
            channel.ReplyUnhandled();

            const auto identity = Run(protocol.Probe());
            Check(identity.has_value(), "a board that answers 0xA0 but refuses 0xA2 is still a Keychron");
            Check(identity && identity->featureBits == 0, "it is simply found with no features");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            // The layer report a 2025q3 board sends on every layer change, arriving mid-request.
            channel.Reply({ 0xA3, 0x00, 0x03 });
            channel.Reply(c_A0);
            channel.Reply(c_A2);
            Check(Run(protocol.Probe()).has_value() && channel.DroppedCount() == 1,
                  "the unsolicited layer report is read past, not taken for the reply");
        }
    }

    void TestFeatureBitsUnderReport()
    {
        std::printf("the feature bits under-report -- only 0xFF means absent\n");

        FakeDeviceChannel  channel;
        keychron::Protocol protocol(channel);

        channel.Reply(c_A0);
        channel.Reply(c_A2);
        const auto identity = Run(protocol.Probe());

        Check(identity && identity->Has(keychron::Feature::AnalogMatrix), "bit 3 says the analog matrix is there");
        Check(identity && !identity->Has(keychron::Feature::KeychronRgb), "bit 7 says there is no Keychron RGB");

        // ... and yet the board answers 0xA8 and 0xA7 0x12. This is the trap: a section gated on
        // the bit would be hidden on a board that has the feature.
        channel.Reply(c_A8Version);
        Check(Run(protocol.SupportsRgb()), "0xA8 answers anyway: RGB is there despite the clear bit");

        channel.Reply(c_A7Nkro);
        Check(Run(protocol.SupportsMisc(keychron::Misc::GetNkro)),
              "0xA7 0x12 answers though every misc bit is clear");

        channel.ReplyUnhandled();
        Check(!Run(protocol.SupportsMisc(keychron::Misc::GetDebounce)),
              "0xA7 0x05 answers 0xFF: debounce really is absent");

        // A 0xA9 sub-id the firmware does not know comes back as the request, as Vial does.
        channel.ReplyEcho();
        Check(!Run(protocol.SupportsAnalogMatrix()), "a 0xA9 sub-id echoed unchanged is a refusal");
    }

    void TestProfileGeometry()
    {
        std::printf("the profile geometry\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply(c_A9Info);
            const keychron::ProfileGeometry geometry = Run(protocol.GetProfileGeometry());

            Check(geometry.currentProfile == 0, "profile 0 is current");
            Check(geometry.profileCount == 3, "three profiles");
            Check(geometry.profileSize == 980, "the size is little-endian: D4 03 is 980, not 54275");
            Check(geometry.dksSlots == 20 && geometry.socdSlots == 20, "20 DKS and 20 SOCD slots");

            Check(geometry.Fits(c_Rows, c_Cols), "980 is the size for 6 x 21 with 20 and 20");
            Check(!geometry.Fits(6, 20), "a matrix one column short does not fit");
            Check(!geometry.Fits(5, 21), "nor one row short");
        }
        {
            Check(keychron::ProfileSizeFor(6, 21, 20, 20) == 980, "2 + 4(126+1) + 30 + 19x20 + 3x20 = 980");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply({ 0xA9, 0x10, 0x00, 0x00, 0x00, 0x00, 0x14, 0x14 });
            Check(Throws<ProtocolError>([&] { Run(protocol.GetProfileGeometry()); }),
                  "a board reporting no profiles is refused, not parsed");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply({ 0xA9, 0x10, 0x05, 0x03, 0xD4, 0x03, 0x14, 0x14 });
            Check(Throws<ProtocolError>([&] { Run(protocol.GetProfileGeometry()); }),
                  "a current profile past the count is refused");
        }
    }

    void TestProfileRead()
    {
        std::printf("reading a profile raw\n");

        FakeDeviceChannel  channel;
        keychron::Protocol protocol(channel);

        keychron::ProfileGeometry geometry;
        geometry.currentProfile = 0;
        geometry.profileCount   = 3;
        geometry.profileSize    = 980;
        geometry.dksSlots       = c_Dks;
        geometry.socdSlots      = c_Socd;

        // 980 = 37 x 26 + 18: the last request asks for the remainder, never past the profile.
        size_t offset = 0;
        while (offset < 980)
        {
            const size_t         size = std::min<size_t>(26, 980 - offset);
            std::vector<uint8_t> reply{ 0xA9, 0x12, 0x00, 0x00, 0x00, static_cast<uint8_t>(size) };
            for (size_t i = 0; i < size; ++i)
                reply.push_back(static_cast<uint8_t>((offset + i) & 0xFF));
            channel.Reply(reply);
            offset += size;
        }

        const std::vector<uint8_t> raw = Run(protocol.GetProfileRaw(0, geometry));
        Check(raw.size() == 980, "980 bytes come back");
        Check(channel.RequestCount() == 38, "in 38 requests: 37 of 26 bytes and one of 18");

        const std::vector<uint8_t>& last = channel.RequestAt(37);
        Check(last[5] == 18, "the last request asks for 18 bytes, not 26 past the profile's end");
        Check(last[3] == static_cast<uint8_t>(962 & 0xFF) && last[4] == (962 >> 8),
              "its offset is little-endian");

        bool placed = true;
        for (size_t i = 0; i < raw.size(); ++i)
            placed = placed && raw[i] == static_cast<uint8_t>(i & 0xFF);
        Check(placed, "the chunks are placed in order, from byte 6 of each reply");

        Check(Throws<std::invalid_argument>([&] { Run(protocol.GetProfileRaw(3, geometry)); }),
              "a profile past the count is refused before anything is sent");
    }

    void TestKeyConfigBitfields()
    {
        std::printf("analog_key_config_t -- the packed bitfields\n");

        // The board's own global word: 45 04 01 00, little-endian 0x00010445. Keychron's Launcher
        // shows 1.7 mm actuation for this profile, which is act_pt 17.
        const keychron::KeyConfig global = keychron::DecodeKeyConfig(0x00010445u);

        Check(global.mode == keychron::KeyMode::Regular, "mode 1, regular");
        Check(global.actuationPoint == 17, "act_pt 17 -- 1.7 mm, as Keychron's Launcher shows");
        Check(global.pressSensitivity == 4, "rapid trigger press sensitivity 0.4 mm");
        Check(global.releaseSensitivity == 4, "release sensitivity 0.4 mm");
        Check(global.advancedMode == keychron::AdvancedMode::None, "no advanced mode");
        Check(global.advancedData == 0, "no advanced data");
        Check(!global.FollowsProfile(), "the global settings are not 'follow the profile'");

        Check(keychron::EncodeKeyConfig(global) == 0x00010445u, "it encodes back to the same word");

        // An untouched key reads 0: follow the profile.
        const keychron::KeyConfig key = keychron::DecodeKeyConfig(0);
        Check(key.FollowsProfile(), "a key left alone follows the profile");
        Check(key.mode == keychron::KeyMode::FollowProfile, "its mode is 0");

        // A per-key word from the same board after Rapid Trigger was ticked in Keychron's
        // Launcher: all 104 keys it has, set to mode 2. `global` never moved.
        const keychron::KeyConfig perKey = keychron::DecodeKeyConfig(0x00010446u);
        Check(perKey.mode == keychron::KeyMode::RapidTrigger, "mode 2, rapid trigger");
        Check(perKey.actuationPoint == 17 && perKey.pressSensitivity == 4 && perKey.releaseSensitivity == 4,
              "with the same 1.7 / 0.4 / 0.4 as the profile's own");
        Check(!perKey.FollowsProfile(), "a key set apart does not follow the profile");
        Check((0x00010446u ^ 0x00010445u) == 0x3u, "it differs from the global word in the mode bits alone");

        // Unticking it wrote global's own word into every key rather than restoring the 0 that
        // means "follow the profile": the board behaves as it did at the start and looks nothing
        // like it. A client must compare effective settings, not words against zero.
        const keychron::KeyConfig afterOff = keychron::DecodeKeyConfig(0x00010445u);
        Check(afterOff.mode == keychron::KeyMode::Regular, "turning it off leaves the key at regular");
        Check(!afterOff.FollowsProfile(), "and NOT back at 'follow the profile'");
        Check(keychron::EncodeKeyConfig(afterOff) == keychron::EncodeKeyConfig(global),
              "the key now carries exactly the profile's own settings");

        // The fields do not bleed into each other. Derived from GCC's packing on ARM, so each is
        // pinned on its own here: a wrong shift would show as a neighbour changing.
        struct Case
        {
            keychron::KeyConfig config;
            uint32_t            word;
            const char*         what;
        };
        const Case cases[] = {
            { { keychron::KeyMode::RapidTrigger, 0, 0, 0, keychron::AdvancedMode::None, 0 }, 0x00000002u,
              "mode is bits 0-1" },
            { { keychron::KeyMode::FollowProfile, 0x3F, 0, 0, keychron::AdvancedMode::None, 0 }, 0x000000FCu,
              "act_pt is bits 2-7, six wide" },
            { { keychron::KeyMode::FollowProfile, 0, 0x3F, 0, keychron::AdvancedMode::None, 0 }, 0x00003F00u,
              "press sensitivity is bits 8-13" },
            { { keychron::KeyMode::FollowProfile, 0, 0, 0x3F, keychron::AdvancedMode::None, 0 }, 0x000FC000u,
              "release sensitivity is bits 14-19" },
            { { keychron::KeyMode::FollowProfile, 0, 0, 0, keychron::AdvancedMode::Toggle, 0 }, 0x00500000u,
              "adv_mode is bits 20-23, and toggle stores as 5" },
            { { keychron::KeyMode::FollowProfile, 0, 0, 0, keychron::AdvancedMode::None, 0xFF }, 0xFF000000u,
              "adv_mode_data is the top byte" },
        };
        for (const Case& c : cases)
        {
            Check(keychron::EncodeKeyConfig(c.config) == c.word, c.what);
            Check(keychron::DecodeKeyConfig(c.word) == c.config, "and it decodes back");
        }
    }

    void TestDecodeProfile()
    {
        std::printf("the whole profile, as the board sent it\n");

        // The K10 HE's profile 0: the global word, then 976 zeroes. Every key follows the profile,
        // no DKS or SOCD slot is in use, the name is empty, and the crc16 is 0 -- the firmware
        // never computes it.
        std::vector<uint8_t> raw(980, 0);
        raw[0] = 0x45;
        raw[1] = 0x04;
        raw[2] = 0x01;
        raw[3] = 0x00;

        const keychron::Profile profile = keychron::DecodeProfile(raw, c_Rows, c_Cols, c_Dks, c_Socd);

        Check(profile.global.actuationPoint == 17, "the global actuation point is 1.7 mm");
        Check(profile.keys.size() == 126, "126 keys, 6 x 21");

        bool allFollow = true;
        for (const keychron::KeyConfig& key : profile.keys)
            allFollow = allFollow && key.FollowsProfile();
        Check(allFollow, "every key on an untouched board follows the profile");

        Check(profile.dksSlots.size() == 20, "20 DKS slots");
        Check(profile.socdSlots.size() == 20, "20 SOCD slots");

        bool slotsEmpty = true;
        for (const keychron::DksSlot& slot : profile.dksSlots)
            slotsEmpty = slotsEmpty && slot.IsEmpty();
        for (const keychron::SocdSlot& slot : profile.socdSlots)
            slotsEmpty = slotsEmpty && slot.IsEmpty();
        Check(slotsEmpty, "none of them is in use");

        Check(profile.name.empty(), "the profile has no name");
        Check(profile.crc16 == 0, "the crc16 is 0: the firmware never computes it");

        Check(Throws<std::invalid_argument>(
                  [&] { (void)keychron::DecodeProfile(raw, 6, 20, c_Dks, c_Socd); }),
              "a profile whose size does not match the matrix is refused, not misread");
    }

    void TestSlotLayouts()
    {
        std::printf("the DKS and SOCD slot layouts\n");

        std::vector<uint8_t> raw(980, 0);

        // DKS slot 0 starts after the global word and the 126 keys.
        const size_t dksAt = 4 + 4 * 126;

        // travel, u24 LE: shallow press 5, shallow release 9, deep press 20, deep release 30.
        const uint32_t travel = 5u | (9u << 6) | (20u << 12) | (30u << 18);
        raw[dksAt]     = static_cast<uint8_t>(travel & 0xFF);
        raw[dksAt + 1] = static_cast<uint8_t>((travel >> 8) & 0xFF);
        raw[dksAt + 2] = static_cast<uint8_t>((travel >> 16) & 0xFF);

        raw[dksAt + 3] = 0x04;   // keycode 0 = 0x0004, KC_A, little-endian
        raw[dksAt + 4] = 0x00;
        raw[dksAt + 11] = 0x62;  // shallow pair for keycode 0: low nibble press 2, high nibble release 6
        raw[dksAt + 12] = 0x17;  // deep pair: press 7, release 1

        // SOCD slot 0, after the DKS slots: key 1 (3, 5), key 2 (4, 17), last wins.
        const size_t socdAt = dksAt + 19 * 20;
        raw[socdAt]     = static_cast<uint8_t>(3u | (5u << 3));
        raw[socdAt + 1] = static_cast<uint8_t>(4u | (17u << 3));
        raw[socdAt + 2] = static_cast<uint8_t>(keychron::SocdType::LastWins);

        // The name, and a crc the firmware would not have written.
        const size_t nameAt = socdAt + 3 * 20;
        const char*  name   = "Gaming";
        for (size_t i = 0; name[i]; ++i)
            raw[nameAt + i] = static_cast<uint8_t>(name[i]);

        const keychron::Profile profile = keychron::DecodeProfile(raw, c_Rows, c_Cols, c_Dks, c_Socd);

        const keychron::DksSlot& slot = profile.dksSlots[0];
        Check(!slot.IsEmpty(), "DKS slot 0 is in use");
        Check(slot.shallowPress == 5, "shallow press is the low 6 bits of the u24");
        Check(slot.shallowRelease == 9, "shallow release is bits 6-11");
        Check(slot.deepPress == 20, "deep press is bits 12-17");
        Check(slot.deepRelease == 30, "deep release is bits 18-23");
        Check(slot.keycodes[0] == 0x0004, "the keycodes are little-endian u16");
        Check(slot.onShallowPress[0] == 2 && slot.onShallowRelease[0] == 6,
              "the shallow pair is low nibble press, high nibble release");
        Check(slot.onDeepPress[0] == 7 && slot.onDeepRelease[0] == 1, "then the deep pair the same way");
        Check(profile.dksSlots[1].IsEmpty(), "slot 1 is still free");

        const keychron::SocdSlot& socd = profile.socdSlots[0];
        Check(socd.firstRow == 3 && socd.firstColumn == 5, "a SOCD key is 3 bits of row, 5 of column");
        Check(socd.secondRow == 4 && socd.secondColumn == 17, "and the second key the same");
        Check(socd.type == keychron::SocdType::LastWins, "the type is byte 2");
        Check(profile.socdSlots[1].IsEmpty(), "an unused SOCD slot reads type 0");

        Check(profile.name == "Gaming", "the name is read up to its NUL");
    }

    void TestCurveAndTravel()
    {
        std::printf("the curve, and the board's full travel\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply(c_A9Curve);
            const std::vector<keychron::CurvePoint> curve = Run(protocol.GetCurve());

            Check(curve.size() == 4, "four points");
            Check(curve[0] == keychron::CurvePoint{ 0, 0 }, "(0, 0)");
            Check(curve[1] == keychron::CurvePoint{ 8, 31 }, "(8, 31) -- not the source's (10, 31)");
            Check(curve[2] == keychron::CurvePoint{ 26, 95 }, "(26, 95) -- not (30, 95)");
            Check(curve[3] == keychron::CurvePoint{ 35, 127 }, "(35, 127) -- not (40, 127)");

            Check(keychron::EstimateFullTravel(curve) == 35,
                  "the K10 HE's full travel is 3.5 mm, measured at the bottom of a key");
        }
        {
            // A board built to the published source.
            const std::vector<keychron::CurvePoint> source{ { 0, 0 }, { 10, 31 }, { 30, 95 }, { 40, 127 } };
            Check(keychron::EstimateFullTravel(source) == 40, "a 4.0 mm board estimates 40");
            Check(keychron::DefaultActuationPoint(40) == 20, "whose default actuation point is 2.0 mm");
        }
        {
            Check(keychron::DefaultActuationPoint(35) == 17,
                  "and the K10 HE's is 1.7 mm: half its travel, floored -- what the board holds");
            Check(keychron::EstimateFullTravel({}) == 0, "no curve, no maximum to offer");
            Check(keychron::EstimateFullTravel({ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 99, 127 } }) == 0,
                  "a curve claiming more than 4.0 mm is not believed");
        }
    }

    void TestLiveReadout()
    {
        std::printf("the live readout and a key's calibration\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            // The space bar at (5, 6) held to the bottom: travel 35 in 0.1 mm, 215 in 1/60 mm --
            // 6 x 35 would be 210, so the two units do not keep a fixed ratio at the bottom.
            channel.Reply({ 0xA9, 0x30, 0x00, 0x05, 0x06, 35, 215, 0xE3, 0x06, 0x4D, 0x0D, 0x41, 0x07, 0x11 });

            const keychron::RealtimeTravel travel = Run(protocol.GetRealtimeTravel(5, 6));
            Check(travel.row == 5 && travel.column == 6, "the key is echoed back");
            Check(travel.travel == 35, "3.5 mm, the bottom of this board's travel");
            Check(travel.travelSixtieths == 215, "215 in 1/60 mm, where 6 x 35 would be 210");
            Check(travel.rawAdc == 1763, "the raw ADC is little-endian");
            Check(travel.restValue == 3405 && travel.bottomValue == 1857, "rest and bottom too");
            Check(travel.pressed, "0x11 in byte 13 is pressed");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply({ 0xA9, 0x30, 0x01 });
            Check(Throws<ProtocolError>([&] { Run(protocol.GetRealtimeTravel(5, 6)); }),
                  "a non-zero status is an error, not a reading");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            // (0, 0) on the board: rest 3472, bottom 1912, scale 10.522985.
            channel.Reply({ 0xA9, 0x42, 0x00, 0x00, 0x00, 0x90, 0x0D, 0x78, 0x07, 0x8D, 0x62, 0x28, 0x41 });
            const keychron::CalibratedValue value = Run(protocol.GetCalibratedValue(0, 0));

            Check(value.restValue == 3472 && value.bottomValue == 1912, "rest and bottom, little-endian");
            Check(value.scaleFactor > 10.52f && value.scaleFactor < 10.53f, "the scale factor is a little-endian float");
            Check(!value.IsAbsent(), "a populated position has a calibration");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            // (0, 1) on the board: no key there.
            channel.Reply({ 0xA9, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xCB });
            Check(Run(protocol.GetCalibratedValue(0, 1)).IsAbsent(),
                  "rest 0 and bottom 0: there is no key at that matrix position");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            Check(Throws<std::invalid_argument>([&] { Run(protocol.GetCalibratedValue(0xFF, 0xFF)); }),
                  "(255, 255) is never sent: the firmware reads out of bounds");
        }
    }

    // The writes have met no board: these pin the request layout the source reads
    // (analog_matrix.c, set_travel()), not a reply recorded from one.
    void TestWrites()
    {
        std::printf("the writes, as the source reads them\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply({ 0xA9, 0x14, 0x00 });
            keychron::KeyConfig config;
            config.mode               = keychron::KeyMode::RapidTrigger;
            config.actuationPoint     = 5;
            config.pressSensitivity   = 2;
            config.releaseSensitivity = 3;
            // W, A, S, D on the K10 HE: (2, 2), (3, 1), (3, 2), (3, 3); and Numpad + at (2, 20).
            Run(protocol.SetKeysTravel(0, config, { { 2, 2 }, { 3, 1 }, { 3, 2 }, { 3, 3 }, { 2, 20 } }));

            const std::vector<uint8_t>& request = channel.RequestAt(0);
            Check(request[0] == 0xA9 && request[1] == 0x14, "AMC_SET_TRAVAL");
            Check(request[2] == 0 && request[3] == 2, "profile 0, mode 2 (rapid trigger)");
            Check(request[4] == 5 && request[5] == 2 && request[6] == 3,
                  "actuation, then press and release sensitivity, in 0.1 mm");
            Check(request[7] == 0, "0: the keys named, not the profile's global");
            Check(request[8 + 3 * 2] == 0x04 && request[8 + 3 * 2 + 2] == 0x10,
                  "row 2: column 2 in its first byte, column 20 in its third");
            Check(request[8 + 3 * 3] == 0x0E, "row 3: columns 1, 2 and 3");
            Check(request[8] == 0 && request[8 + 3 * 5] == 0, "rows without a key named stay 0");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply({ 0xA9, 0x14, 0x01 });
            keychron::KeyConfig config;
            config.mode           = keychron::KeyMode::Regular;
            config.actuationPoint = 17;
            Check(Throws<ProtocolError>([&] { Run(protocol.SetKeysTravel(0, config, { { 0, 0 } })); }),
                  "a non-zero status is refused");
            Check(Throws<std::invalid_argument>([&] { Run(protocol.SetKeysTravel(0, config, { { 0, 24 } })); }),
                  "a column past 23 cannot be named in one write");
            config.mode = keychron::KeyMode::FollowProfile;
            Check(Throws<std::invalid_argument>([&] { Run(protocol.SetKeysTravel(0, config, { { 0, 0 } })); }),
                  "a key is written with a mode of its own");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply({ 0xA9, 0x11, 0x00 });
            channel.Reply({ 0xA9, 0x1F, 0x00 });
            channel.Reply({ 0xA9, 0x1E, 0x00 });
            Run(protocol.SelectProfile(1));
            Run(protocol.SaveProfile(1));
            Run(protocol.ResetProfile(2));
            Check(channel.RequestAt(0)[1] == 0x11 && channel.RequestAt(0)[2] == 1, "AMC_SELECT_PROFILE, the profile");
            Check(channel.RequestAt(1)[1] == 0x1F && channel.RequestAt(1)[2] == 1, "AMC_SAVE_PROFILE, the profile");
            Check(channel.RequestAt(2)[1] == 0x1E && channel.RequestAt(2)[2] == 2, "AMC_RESET_PROFILE, the profile");
        }
    }

    // Planning a change on the three states profile 0 was read in (2026-11-08).
    void TestPlanTravel()
    {
        std::printf("planning a change: keys told apart by what they do\n");

        const keychron::KeyConfig regular = keychron::DecodeKeyConfig(0x00010445u);   // regular, 1.7, 0.4, 0.4

        keychron::Profile asItCame;
        asItCame.global = regular;
        asItCame.keys.assign(static_cast<size_t>(c_Rows) * c_Cols, keychron::KeyConfig{});

        keychron::Profile untickedAgain = asItCame;
        for (keychron::KeyConfig& key : untickedAgain.keys)
            key = regular;

        const keychron::KeyPosition w{ 2, 2 }, a{ 3, 1 };
        Check(keychron::EffectiveConfig(asItCame, w, c_Cols) == keychron::EffectiveConfig(untickedAgain, w, c_Cols),
              "a key at 0 and a key holding the global word do the same, and read the same");
        Check(keychron::EffectiveConfig(asItCame, w, c_Cols).mode == keychron::KeyMode::Regular &&
                  keychron::EffectiveConfig(asItCame, w, c_Cols).actuationPoint == 17,
              "a key at 0 takes the profile's 1.7 mm");

        // Rapid trigger on W and A: one write, every field explicit.
        keychron::TravelChange change;
        change.mode = keychron::KeyMode::RapidTrigger;
        std::vector<keychron::TravelWrite> writes = keychron::PlanTravelWrites(asItCame, c_Cols, { w, a }, change);
        Check(writes.size() == 1 && writes[0].keys.size() == 2, "two keys alike: one write");
        Check(writes[0].config.mode == keychron::KeyMode::RapidTrigger && writes[0].config.actuationPoint == 17 &&
                  writes[0].config.pressSensitivity == 4 && writes[0].config.releaseSensitivity == 4,
              "the rest of each key's settings written as they are, not left at 0");

        // W apart (rapid, 0.5 mm), then a new actuation for W and A: the sensitivities differ no
        // more, the modes still do -- two writes.
        keychron::Profile apart = untickedAgain;
        keychron::ApplyTravelWrite(apart, c_Cols, { keychron::DecodeKeyConfig(0x00010216u), { w } });
        Check(keychron::EffectiveConfig(apart, w, c_Cols).mode == keychron::KeyMode::RapidTrigger &&
                  keychron::EffectiveConfig(apart, w, c_Cols).actuationPoint == 5,
              "a write applied lands on its key");
        keychron::TravelChange deeper;
        deeper.actuationPoint = 20;
        writes = keychron::PlanTravelWrites(apart, c_Cols, { w, a }, deeper);
        Check(writes.size() == 2, "keys left differing by the change take a write each");
        Check(writes[0].config.actuationPoint == 20 && writes[1].config.actuationPoint == 20,
              "both at the new actuation point");

        Check(Throws<std::invalid_argument>([&] { (void)keychron::EffectiveConfig(apart, { 6, 0 }, c_Cols); }),
              "a key outside the matrix is refused");
    }

    void TestTopLevel()
    {
        std::printf("the top-level commands\n");

        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            std::vector<uint8_t> reply{ 0xA1 };
            for (const char* s = "v1.0.0 2026-07-17-17:42:44"; *s; ++s)
                reply.push_back(static_cast<uint8_t>(*s));
            channel.Reply(reply);

            Check(Run(protocol.GetFirmwareVersion()) == "v1.0.0 2026-07-17-17:42:44",
                  "the firmware version is ASCII from byte 1, to its NUL");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply(c_A3);
            Check(Run(protocol.GetDefaultLayer()) == 2, "the default layer is 2, the Windows base layer");
        }
        {
            FakeDeviceChannel  channel;
            keychron::Protocol protocol(channel);

            channel.Reply(c_A9Version);
            Check(Run(protocol.GetAnalogVersion()) == 4, "the analog matrix is version 4");
        }
    }
}

int main()
{
    ConfigureCrtReporting();

    TestProbe();
    TestFeatureBitsUnderReport();
    TestProfileGeometry();
    TestProfileRead();
    TestKeyConfigBitfields();
    TestDecodeProfile();
    TestSlotLayouts();
    TestCurveAndTravel();
    TestLiveReadout();
    TestWrites();
    TestPlanTravel();
    TestTopLevel();

    return TestResult();
}
