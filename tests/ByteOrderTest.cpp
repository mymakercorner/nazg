// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the byte-order readers directly, so a mistake in them fails here rather than
// showing up as one protocol command returning a nonsense value.
//
// The same device uses both orders -- see the header -- so every case below reads the
// SAME bytes in both directions and checks they disagree in the expected way.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/NazgByteOrder.h"

#include "TestSupport.h"

#include <cstdint>
#include <vector>

using namespace nazg;

namespace
{
    // 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 at offset 1, with a marker byte in front
    // so an off-by-one in the offset is visible rather than silently harmless.
    const std::vector<uint8_t> c_Bytes = { 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };

    void TestBigEndian()
    {
        std::printf("big-endian (VIA scalars, and keycodes everywhere)\n");

        Check(ReadBigEndian16(c_Bytes, 1) == 0x0102, "16-bit takes the first byte as most significant");
        Check(ReadBigEndian32(c_Bytes, 1) == 0x01020304u, "32-bit reads left to right");
        Check(ReadBigEndian16(c_Bytes, 0) == 0xFF01, "the offset is honoured, high bit and all");
    }

    void TestLittleEndian()
    {
        std::printf("little-endian (Vial's own scalars)\n");

        Check(ReadLittleEndian16(c_Bytes, 1) == 0x0201, "16-bit takes the first byte as least significant");
        Check(ReadLittleEndian32(c_Bytes, 1) == 0x04030201u, "32-bit reads right to left");
        Check(ReadLittleEndian64(c_Bytes, 1) == 0x0807060504030201ull, "64-bit covers the keyboard UID");
    }

    // The two orders must not agree on anything asymmetric, which is what makes a
    // mixed-up call site show up as a wrong value rather than a coincidence.
    void TestTheTwoOrdersDisagree()
    {
        std::printf("the two orders disagree\n");

        Check(ReadBigEndian16(c_Bytes, 1) != ReadLittleEndian16(c_Bytes, 1), "16-bit differs");
        Check(ReadBigEndian32(c_Bytes, 1) != ReadLittleEndian32(c_Bytes, 1), "32-bit differs");
    }

    // Real values seen on hardware, so the helpers are checked against bytes a board
    // actually sent rather than only against invented ones.
    void TestValuesSeenOnHardware()
    {
        std::printf("values seen on hardware\n");

        // The Aquanaut answering id_get_protocol_version: 01 00 0C -> 12, big-endian.
        const std::vector<uint8_t> viaVersion = { 0x01, 0x00, 0x0C };
        Check(ReadBigEndian16(viaVersion, 1) == 12, "VIA protocol 12 parses big-endian");

        // The Model F answering vial_get_keyboard_id: 06 00 00 00 -> 6, little-endian.
        const std::vector<uint8_t> vialVersion = { 0x06, 0x00, 0x00, 0x00 };
        Check(ReadLittleEndian32(vialVersion, 0) == 6, "Vial protocol 6 parses little-endian");
        Check(ReadBigEndian32(vialVersion, 0) != 6, "and would be nonsense read the other way");

        // The same board's definition size: C0 02 00 00 -> 704 bytes.
        const std::vector<uint8_t> definitionSize = { 0xC0, 0x02, 0x00, 0x00 };
        Check(ReadLittleEndian32(definitionSize, 0) == 704, "the 704-byte definition size parses");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestBigEndian();
    TestLittleEndian();
    TestTheTwoOrdersDisagree();
    TestValuesSeenOnHardware();

    return TestResult();
}
