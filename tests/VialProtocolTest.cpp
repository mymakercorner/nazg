// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Vial branch: detection (including correctly reporting "this is a plain VIA
// board"), the paged definition download, and the little-endian scalars that are the
// easiest thing to get backwards, since VIA's are big-endian on the same device.
//
// Byte layouts come from docs/research_material/via-vial-commands.md, read out of
// vial-qmk's vial.c.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/vial/NazgVialProtocol.h"
#include "adapters/vial/NazgVialLoader.h"
#include "model/NazgCombo.h"
#include "model/NazgTapDance.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <vector>

using nazg::ProtocolError;
using nazg::Task;
using nazg::VialProtocol;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");

        return task.TakeResult();
    }

    template <typename TCall>
    bool Throws(TCall&& call)
    {
        try
        {
            call();
        }
        catch (const ProtocolError&)
        {
            return true;
        }
        return false;
    }

    void TestDetectOnVialBoard()
    {
        std::printf("detect on a Vial board\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // [0..3] protocol version little-endian, [4..11] keyboard UID, [12] VialRGB.
        channel.Reply({ 0x06, 0x00, 0x00, 0x00,
                        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x01 });

        const auto identity = Run(vial.Detect());

        Check(identity.has_value(), "a Vial board is detected");
        Check(identity->protocolVersion == 6, "the version is parsed little-endian");
        Check(identity->keyboardUid == 0x0807060504030201ull, "the UID is parsed little-endian");
        Check(identity->supportsVialRgb, "the VialRGB flag is byte 12");

        const std::vector<uint8_t>& request = channel.RequestAt(0);
        Check(request[0] == 0xFE, "the Vial prefix leads the report");
        Check(request[1] == 0x00, "the sub-command follows it");
    }

    // The case that matters for a mixed fleet: the same call on plain VIA firmware has
    // to come back "not Vial" rather than throwing or returning nonsense.
    void TestDetectOnViaBoard()
    {
        std::printf("detect on a plain VIA board\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.ReplyUnhandled();

        const auto identity = Run(vial.Detect());

        Check(!identity.has_value(), "a VIA board reports no Vial support");
    }

    void TestDefinitionDownload()
    {
        std::printf("definition download\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // 70 bytes is three pages: 32 + 32 + 6, with the last one truncated.
        channel.Reply({ 70, 0x00, 0x00, 0x00 });

        for (uint8_t marker = 1; marker <= 3; ++marker)
            channel.Reply(std::vector<uint8_t>(nazg::c_ViaReportSize, marker));

        const std::vector<uint8_t> definition = Run(vial.DownloadDefinition());

        Check(channel.RequestCount() == 4, "one size query plus three pages");
        Check(definition.size() == 70, "the download stops at the reported size");
        Check(definition[0] == 1 && definition[31] == 1, "page 0 lands first");
        Check(definition[32] == 2 && definition[63] == 2, "page 1 follows");
        Check(definition[64] == 3 && definition[69] == 3, "the last page is truncated to what remains");

        Check(channel.RequestAt(1)[2] == 0 && channel.RequestAt(1)[3] == 0, "page 0 requested");
        Check(channel.RequestAt(2)[2] == 1 && channel.RequestAt(2)[3] == 0,
              "the page index is little-endian, unlike VIA's big-endian offsets");
        Check(channel.RequestAt(3)[2] == 2 && channel.RequestAt(3)[3] == 0, "page 2 requested");
    }

    void TestImplausibleDefinitionSizeIsRefused()
    {
        std::printf("implausible definition size\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.Reply({ 0xFF, 0xFF, 0xFF, 0x7F });   // ~2 GB

        Check(Throws([&] { Run(vial.DownloadDefinition()); }),
              "a nonsense size is refused instead of allocated");
        Check(channel.RequestCount() == 1, "no pages were requested");
    }

    // The capability query that replaces guessing from the version number -- alt repeat
    // key was added in 2025 without bumping the protocol past 6.
    void TestEntryCounts()
    {
        std::printf("dynamic entry counts\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0x00);
        reply[0] = 8;    // tap dance
        reply[1] = 16;   // combo
        reply[2] = 4;    // key override
        reply[3] = 0;    // alt repeat key: compiled out
        reply[31] = 0x03;
        channel.ReplyRaw(reply);

        const auto counts = Run(vial.GetEntryCounts());

        Check(counts.tapDance == 8 && counts.combo == 16, "the counts are read in order");
        Check(counts.keyOverride == 4, "key overrides counted");
        Check(counts.altRepeatKey == 0, "a zero count means the feature is compiled out");
        Check(counts.capsWord && counts.layerLock, "the feature bits are in the LAST byte");

        Check(channel.RequestAt(0)[1] == 0x0D && channel.RequestAt(0)[2] == 0x00,
              "the entry op is addressed by sub-command then operation");
    }

    // A tap dance slot: a status byte, then the entry as stored -- little-endian, keycodes too.
    void TestTapDance()
    {
        std::printf("tap dance get and set\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0x00);
        const uint8_t entry[] = { 0x29, 0x00,  0x21, 0x52,  0x39, 0x00,  0x00, 0x00,  0xC8, 0x00 };
        std::copy(std::begin(entry), std::end(entry), reply.begin() + 1);
        channel.ReplyRaw(reply);

        const nazg::VialTapDanceEntry read = Run(vial.GetTapDance(3));
        Check(read.onTap == 0x0029 && read.onHold == 0x5221, "keycodes are little-endian here, unlike the keymap's");
        Check(read.onDoubleTap == 0x0039 && read.onTapHold == 0, "an empty action is 0");
        Check(read.tappingTerm == 200, "the slot's own term follows the keycodes");
        Check(channel.RequestAt(0)[1] == 0x0D && channel.RequestAt(0)[2] == 0x01 && channel.RequestAt(0)[3] == 3,
              "get is the entry op's 0x01, then the slot");

        channel.ReplyRaw(std::vector<uint8_t>(nazg::c_ViaReportSize, 0x00));
        Run(vial.SetTapDance(5, read));
        const std::vector<uint8_t> sent = channel.RequestAt(1);
        Check(sent[2] == 0x02 && sent[3] == 5, "set is the entry op's 0x02, then the slot");
        Check(std::equal(std::begin(entry), std::end(entry), sent.begin() + 4), "the entry follows, as the board stores it");

        // As Nazg models it: 0 is an empty action, and the entry encodes back unchanged.
        const nazg::TapDance dance = nazg::DecodeTapDance(read, nazg::QmkKeycodeVersion::V0_0_7);
        Check(dance[nazg::DanceAction::Tap] == nazg::Keycode{ nazg::NamedKey{ "KC_ESC" } } &&
                  dance[nazg::DanceAction::Hold] == nazg::Keycode{ nazg::LayerKey{ nazg::LayerOp::Momentary, 1 } } &&
                  !dance[nazg::DanceAction::TapHold] && dance.tappingTerm == 200,
              "decoded: Esc, L1 held, Caps twice, nothing tapped then held");
        Check(nazg::EncodeTapDance(dance, nazg::QmkKeycodeVersion::V0_0_7) == read, "and encoded back unchanged");

        std::vector<uint8_t> refused(nazg::c_ViaReportSize, 0x00);
        refused[0] = 0xFF;
        channel.ReplyRaw(refused);
        Check(Throws([&] { Run(vial.GetTapDance(40)); }), "a slot past the count answers a non-zero status");
    }

    // A combo slot: a status byte, then four inputs and the output, little-endian as the tap dances.
    void TestCombo()
    {
        std::printf("combo get and set\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // J + K (0x0D, 0x0E) for Esc (0x29); the other two inputs empty.
        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0x00);
        const uint8_t entry[] = { 0x0D, 0x00,  0x0E, 0x00,  0x00, 0x00,  0x00, 0x00,  0x29, 0x00 };
        std::copy(std::begin(entry), std::end(entry), reply.begin() + 1);
        channel.ReplyRaw(reply);

        const nazg::VialComboEntry read = Run(vial.GetCombo(2));
        Check(read.inputs[0] == 0x000D && read.inputs[1] == 0x000E && read.inputs[2] == 0 && read.inputs[3] == 0,
              "the four inputs, an empty one 0");
        Check(read.output == 0x0029, "the output follows them");
        Check(channel.RequestAt(0)[2] == 0x03 && channel.RequestAt(0)[3] == 2, "get is the entry op's 0x03, then the slot");

        channel.ReplyRaw(std::vector<uint8_t>(nazg::c_ViaReportSize, 0x00));
        Run(vial.SetCombo(7, read));
        const std::vector<uint8_t> sent = channel.RequestAt(1);
        Check(sent[2] == 0x04 && sent[3] == 7, "set is the entry op's 0x04, then the slot");
        Check(std::equal(std::begin(entry), std::end(entry), sent.begin() + 4), "the entry follows, as the board stores it");

        const nazg::Combo combo = nazg::DecodeCombo(read, nazg::QmkKeycodeVersion::V0_0_7);
        Check(combo.MatchedInputs() == std::vector<nazg::Keycode>{ nazg::NamedKey{ "KC_J" }, nazg::NamedKey{ "KC_K" } } &&
                  combo.output == nazg::Keycode{ nazg::NamedKey{ "KC_ESC" } },
              "decoded: J and K for Esc");
        Check(nazg::EncodeCombo(combo, nazg::QmkKeycodeVersion::V0_0_7) == read, "and encoded back unchanged");

        // A gap the board holds stays one, both ways.
        nazg::VialComboEntry gap = read;
        gap.inputs               = { 0x001D, 0x0000, 0x001B, 0x0000 };
        Check(nazg::DecodeCombo(gap, nazg::QmkKeycodeVersion::V0_0_7).HasGap(), "Z, nothing, X: a gap");
        Check(nazg::EncodeCombo(nazg::DecodeCombo(gap, nazg::QmkKeycodeVersion::V0_0_7), nazg::QmkKeycodeVersion::V0_0_7) == gap,
              "written back where it was");

        std::vector<uint8_t> refused(nazg::c_ViaReportSize, 0x00);
        refused[0] = 0xFF;
        channel.ReplyRaw(refused);
        Check(Throws([&] { Run(vial.GetCombo(40)); }), "a slot past the count answers a non-zero status");
    }

    // One QMK setting: its id little-endian; the value after a status byte, as wide as the setting
    // -- the firmware leaves the rest of the request in place.
    void TestQmkSetting()
    {
        std::printf("QMK setting get and set\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // The request comes back with the status and a u16 written over its start.
        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0x00);
        reply[0] = 0x00;                    // status
        reply[1] = 0x2C; reply[2] = 0x01;   // 300 ms
        reply[3] = 0x00;                    // the request's id, high byte, left in place
        channel.ReplyRaw(reply);
        Check(Run(vial.GetQmkSetting(nazg::c_QmkSettingComboTerm, 2)) == 300, "a u16 setting reads two bytes");
        Check(channel.RequestAt(0)[1] == 0x0A && channel.RequestAt(0)[2] == 0x02 && channel.RequestAt(0)[3] == 0x00,
              "get is 0x0A, then the id, little-endian");

        channel.ReplyRaw(std::vector<uint8_t>(nazg::c_ViaReportSize, 0x00));
        Run(vial.SetQmkSetting(nazg::c_QmkSettingComboTerm, 45));
        const std::vector<uint8_t> sent = channel.RequestAt(1);
        Check(sent[1] == 0x0B && sent[2] == 0x02 && sent[3] == 0x00, "set is 0x0B, then the id");
        Check(sent[4] == 45 && sent[5] == 0 && sent[6] == 0 && sent[7] == 0, "then the value, little-endian");

        std::vector<uint8_t> refused(nazg::c_ViaReportSize, 0x00);
        refused[0] = 0xFF;
        channel.ReplyRaw(refused);
        Check(Throws([&] { Run(vial.GetQmkSetting(0x7777, 2)); }), "a setting the firmware lacks: a non-zero status");
    }

    // The QMK settings a firmware has, above the id asked from: the ids, 0xFFFF filling the rest.
    void TestQmkSettingsQuery()
    {
        std::printf("QMK settings query\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0xFF);
        reply[0] = 0x03; reply[1] = 0x00;
        reply[2] = 0x15; reply[3] = 0x01;   // 0x0115: the ids are little-endian
        channel.ReplyRaw(reply);

        const std::vector<uint16_t> ids = Run(vial.QueryQmkSettings(0x0102));
        Check(ids == std::vector<uint16_t>{ 0x0003, 0x0115 }, "the ids before the 0xFFFF fill");
        Check(channel.RequestAt(0)[1] == 0x09 && channel.RequestAt(0)[2] == 0x02 && channel.RequestAt(0)[3] == 0x01,
              "the id to start after, little-endian");

        channel.ReplyRaw(std::vector<uint8_t>(nazg::c_ViaReportSize, 0xFF));
        Check(Run(vial.QueryQmkSettings(0)).empty(), "compiled out: a report of 0xFF, no ids");
    }

    void TestUnlockStatus()
    {
        std::printf("unlock status\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // The firmware fills the report with 0xFF, then writes status and combo over it.
        std::vector<uint8_t> reply(nazg::c_ViaReportSize, 0xFF);
        reply[0] = 0;    // locked
        reply[1] = 1;    // unlock in progress
        reply[2] = 3; reply[3] = 4;
        reply[4] = 5; reply[5] = 6;
        channel.ReplyRaw(reply);

        const auto status = Run(vial.GetUnlockStatus());

        Check(!status.unlocked, "the board reports itself locked");
        Check(status.inProgress, "an unlock is in progress");
        Check(status.unlockKeys.size() == 2, "the 0xFF fill ends the list of unlock keys");
        Check(status.unlockKeys[0] == std::make_pair<uint8_t, uint8_t>(3, 4), "first key is (row, column)");
        Check(status.unlockKeys[1] == std::make_pair<uint8_t, uint8_t>(5, 6), "second key follows");
    }

    // Start, poll, lock: start and lock answer with their own request echoed, which must
    // not be taken for a missing feature.
    void TestUnlockCommands()
    {
        std::printf("unlock commands\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        std::vector<uint8_t> startEcho(nazg::c_ViaReportSize, 0x00);
        startEcho[0] = 0xFE;
        startEcho[1] = 0x06;
        channel.ReplyRaw(startEcho);

        std::vector<uint8_t> poll(nazg::c_ViaReportSize, 0x00);
        poll[0] = 0;    // not unlocked yet
        poll[1] = 1;    // in progress
        poll[2] = 37;   // steps left
        channel.ReplyRaw(poll);

        std::vector<uint8_t> lockEcho(nazg::c_ViaReportSize, 0x00);
        lockEcho[0] = 0xFE;
        lockEcho[1] = 0x08;
        channel.ReplyRaw(lockEcho);

        Check(!Throws([&] { Run(vial.StartUnlock()); }), "an echoed start is its normal answer");
        const auto progress = Run(vial.PollUnlock());
        Check(!Throws([&] { Run(vial.Lock()); }), "an echoed lock is its normal answer");

        Check(!progress.unlocked && progress.inProgress && progress.countdown == 37,
              "a poll reports unlocked, in progress and the steps left");
        Check(channel.RequestCount() == 3 && channel.RequestAt(0)[1] == 0x06 && channel.RequestAt(1)[1] == 0x07 &&
                  channel.RequestAt(2)[1] == 0x08,
              "start is 0x06, poll 0x07, lock 0x08");
    }

    // Vial does not use VIA's 0xFF marker. A sub-command the firmware was built without
    // falls through its switch untouched, so the request comes back byte for byte.
    // Found on real hardware: a board with no encoders "returned" a keycode of 0xFE03,
    // which is the prefix and sub-command being read back.
    void TestUnsupportedSubCommandEchoesTheRequest()
    {
        std::printf("unsupported Vial sub-command\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        // Exactly what GetEncoder(0, 0) sends: prefix, sub-command, layer, index.
        std::vector<uint8_t> echo(nazg::c_ViaReportSize, 0x00);
        echo[0] = 0xFE;
        echo[1] = 0x03;
        channel.ReplyRaw(echo);

        Check(Throws([&] { Run(vial.GetEncoder(0, 0)); }),
              "an echoed request is reported as unsupported, not parsed as data");
    }

    // Detection has to survive the same convention: a device that echoes rather than
    // answering 0xFF is still "not a Vial board", not an error.
    void TestDetectOnEchoingDevice()
    {
        std::printf("detect on an echoing device\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        std::vector<uint8_t> echo(nazg::c_ViaReportSize, 0x00);
        echo[0] = 0xFE;
        echo[1] = 0x00;
        channel.ReplyRaw(echo);

        Check(!Run(vial.Detect()).has_value(), "an echoed detect reports no Vial support");
    }

    void TestEncoderReturnsBothDirections()
    {
        std::printf("encoder\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.Reply({ 0x00, 0x2A, 0x00, 0x2B });

        const auto encoder = Run(vial.GetEncoder(1, 0));

        Check(encoder.counterClockwise == 0x002A, "counter-clockwise comes first");
        Check(encoder.clockwise == 0x002B, "clockwise follows, both big-endian");
        Check(channel.RequestCount() == 1, "Vial returns both directions in ONE round trip, unlike VIA");
    }

    // The point of deriving from ViaProtocol: the shared commands are not reimplemented.
    void TestInheritedViaCommands()
    {
        std::printf("inherited VIA commands\n");

        FakeDeviceChannel channel;
        VialProtocol      vial(channel);

        channel.Reply({ 0x01, 0x00, 0x09 });
        const uint16_t version = Run(vial.GetProtocolVersion());

        Check(version == 9, "Vial firmware reports VIA protocol 9 through the inherited command");

        channel.Reply({ 0x11, 4 });
        Check(Run(vial.GetLayerCount()) == 4, "layer count works unchanged on the Vial branch");
    }

    // The keycode table follows the VIAL protocol. The VIA one is useless for this:
    // vial-qmk always reports 9, the test above, whatever its keycodes are.
    void TestKeycodeVersionFollowsVialProtocol()
    {
        std::printf("keycode version from the Vial protocol\n");

        using nazg::QmkKeycodeVersion;
        using nazg::QmkKeycodeVersionForVial;

        Check(QmkKeycodeVersionForVial(6) == QmkKeycodeVersion::V0_0_7,
              "protocol 6 -- the Model F -- uses vial-qmk's keycodes, 0.0.7");
        Check(QmkKeycodeVersionForVial(5) == QmkKeycodeVersion::Legacy,
              "protocol 5 is pre-renumbering, with TO's ON_PRESS bit as vial-gui's v5 table has it");
        Check(QmkKeycodeVersionForVial(0) == QmkKeycodeVersion::Legacy, "and so is anything older");
    }

    // What the keycode picker and the lighting policy read after a load: VIA's protocol, Vial's
    // and its entry counts, the macro count -- each refusal leaving its field at zero.
    void TestBoardReport()
    {
        std::printf("board report\n");

        {
            FakeDeviceChannel channel;
            VialProtocol      vial(channel);

            channel.Reply({ 0x01, 0x00, 0x09 });   // VIA protocol 9, as vial-qmk always says
            channel.Reply({ 0x06, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00 });
            std::vector<uint8_t> counts(nazg::c_ViaReportSize, 0x00);
            counts[0]  = 8;      // tap dance
            counts[1]  = 6;      // combos
            counts[2]  = 4;      // key overrides
            counts[3]  = 2;      // alt repeat key
            counts[31] = 0x01;   // Caps Word, no Layer Lock
            channel.ReplyRaw(counts);
            std::vector<uint8_t> settings(nazg::c_ViaReportSize, 0xFF);   // QMK settings 1 and 2
            settings[0] = 1; settings[1] = 0;
            settings[2] = 2; settings[3] = 0;
            channel.ReplyRaw(settings);
            channel.Reply({ 0x0C, 16 });   // macros

            const nazg::BoardReport report = Run(nazg::ReadBoardReport(vial));
            Check(report.viaProtocol == 9 && report.isVial && report.vialProtocol == 6, "a Vial 6 board");
            Check(report.tapDanceCount == 8 && report.comboCount == 6 && report.keyOverrideCount == 4 &&
                      report.altRepeatKeyCount == 2,
                  "its entry counts");
            Check(report.capsWord && !report.layerLock, "its feature bits");
            Check(report.hasQmkSettings, "its QMK settings");
            Check(report.macroCount == 16, "and its macro count");
        }

        {
            FakeDeviceChannel channel;
            VialProtocol      vial(channel);

            channel.Reply({ 0x01, 0x00, 0x05 });   // Vial 5 on VIA 9's numbering
            channel.Reply({ 0x05, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00 });
            std::vector<uint8_t> echo(nazg::c_ViaReportSize, 0x00);   // no dynamic entries in this build
            echo[0] = 0xFE;
            echo[1] = 0x0D;
            channel.ReplyRaw(echo);
            channel.ReplyRaw(std::vector<uint8_t>(nazg::c_ViaReportSize, 0xFF));   // QMK settings compiled out
            channel.Reply({ 0x0C, 4 });

            const nazg::BoardReport report = Run(nazg::ReadBoardReport(vial));
            Check(report.isVial && report.vialProtocol == 5 && report.tapDanceCount == 0 && !report.capsWord,
                  "a build without entry counts reports none, not an error");
            Check(!report.hasQmkSettings, "QMK settings compiled out: none");
            Check(report.macroCount == 4, "and the rest is still read");
        }

        {
            FakeDeviceChannel channel;
            VialProtocol      vial(channel);

            channel.Reply({ 0x01, 0x00, 0x0C });   // VIA 12
            channel.ReplyUnhandled();              // not Vial
            channel.ReplyUnhandled();              // macros compiled out

            const nazg::BoardReport report = Run(nazg::ReadBoardReport(vial));
            Check(report.viaProtocol == 12 && !report.isVial, "a VIA 12 board");
            Check(report.macroCount == 0, "macros compiled out: none");
        }
    }
}

int main()
{
    ConfigureCrtReporting();

    TestDetectOnVialBoard();
    TestDetectOnViaBoard();
    TestDefinitionDownload();
    TestImplausibleDefinitionSizeIsRefused();
    TestEntryCounts();
    TestTapDance();
    TestCombo();
    TestQmkSetting();
    TestQmkSettingsQuery();
    TestUnlockStatus();
    TestUnlockCommands();
    TestUnsupportedSubCommandEchoesTheRequest();
    TestDetectOnEchoingDevice();
    TestEncoderReturnsBothDirections();
    TestInheritedViaCommands();
    TestKeycodeVersionFollowsVialProtocol();
    TestBoardReport();

    return TestResult();
}
