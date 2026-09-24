// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The bundle of VIA's official definitions: finding a board's file by VID:PID and
// protocol, inside a tar, inside an XZ stream.
//
// ViaBundleFixture.h is a real bundle in miniature, made by the same commands as the
// full one, so the XZ framing and GNU tar's headers are what minlzma and the tar reader
// will meet. The tar reader's failure cases are built in memory here instead: a header
// is only 512 bytes of fixed fields.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgViaBundle.h"

#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaProtocol.h"

#include "IsoMacroDefinition.h"
#include "TestSupport.h"
#include "ViaBundleFixture.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using nazg::ExtractTarFile;
using nazg::FindViaDefinition;
using nazg::ParseDefinition;
using nazg::ProtocolError;
using nazg::ViaDefinitionPath;

namespace
{
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

    std::vector<uint8_t> Bytes(const std::string& text)
    {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    // One ustar entry -- header and padded data -- with a correct checksum.
    std::vector<uint8_t> TarEntry(const std::string& name, const std::string& data, char type = '0',
                                  const std::string& prefix = "")
    {
        std::vector<uint8_t> header(512, 0);
        std::memcpy(header.data(), name.data(), name.size());
        std::snprintf(reinterpret_cast<char*>(header.data() + 124), 12, "%011o", static_cast<unsigned>(data.size()));
        header[156] = static_cast<uint8_t>(type);
        std::memcpy(header.data() + 257, "ustar\0" "00", 8);
        std::memcpy(header.data() + 345, prefix.data(), prefix.size());

        unsigned sum = 0;
        for (size_t i = 0; i < 512; ++i)
            sum += (i >= 148 && i < 156) ? ' ' : header[i];
        std::snprintf(reinterpret_cast<char*>(header.data() + 148), 8, "%06o", sum);
        header[155] = ' ';

        std::vector<uint8_t> entry = header;
        entry.insert(entry.end(), data.begin(), data.end());
        entry.resize((entry.size() + 511) / 512 * 512, 0);
        return entry;
    }

    std::vector<uint8_t> Archive(std::initializer_list<std::vector<uint8_t>> entries)
    {
        std::vector<uint8_t> tar;
        for (const std::vector<uint8_t>& entry : entries)
            tar.insert(tar.end(), entry.begin(), entry.end());
        tar.resize(tar.size() + 1024, 0);   // the two zero blocks that end an archive
        return tar;
    }

    void TestDefinitionPath()
    {
        std::printf("definition path\n");

        Check(ViaDefinitionPath(0x4D65, 0x1200, 12) == "v3/1298469376.json",
              "vendorId * 65536 + productId, in decimal, under v3/");
        Check(ViaDefinitionPath(0x4D65, 0x1200, 11) == "v3/1298469376.json", "protocol 11 is the first to use V3");
        Check(ViaDefinitionPath(0x4D65, 0x1200, 10) == "v2/1298469376.json", "protocol 10 and below use V2");
        Check(ViaDefinitionPath(0xFFFF, 0xFFFF, 12) == "v3/4294967295.json", "the largest id does not overflow");
    }

    void TestBundle()
    {
        std::printf("the bundle\n");

        const std::vector<uint8_t> bundle = ViaBundleFixture();

        const auto v3 = FindViaDefinition(bundle, 0x4D65, 0x1200, 12);
        Check(v3.has_value() && *v3 == IsoMacroConverted(), "a V3 board gets its file byte for byte");
        Check(v3.has_value() && ParseDefinition(*v3).name == "ISO Macro", "and it parses");

        const auto v2 = FindViaDefinition(bundle, 0x4D65, 0x1200, 10);
        Check(v2.has_value() && ParseDefinition(*v2).vendorId == 0x4D65, "a protocol 10 board gets the V2 file");

        Check(!FindViaDefinition(bundle, 0x1209, 0x4704, 12).has_value(), "a board VIA does not know gets nothing");

        std::vector<uint8_t> damaged = bundle;
        damaged.resize(damaged.size() / 2);
        Check(Throws([&] { (void)FindViaDefinition(damaged, 0x4D65, 0x1200, 12); }), "a truncated bundle is rejected");
        Check(Throws([&] { (void)FindViaDefinition({}, 0x4D65, 0x1200, 12); }), "an empty bundle is rejected");
    }

    void TestTarReader()
    {
        std::printf("tar reader\n");

        // A 600-byte file spills into a second block, so the next header is two blocks on.
        const std::string long600(600, 'x');
        const std::vector<uint8_t> tar = Archive({ TarEntry("v3/", "", '5'), TarEntry("v3/a.json", long600),
                                                   TarEntry("./v3/b.json", "{}"),
                                                   TarEntry("c.json", "[]", '0', "deeply/nested") });

        const auto a = ExtractTarFile(tar, "v3/a.json");
        Check(a.has_value() && a->size() == 600, "a file is found and has its full size");

        const auto b = ExtractTarFile(tar, "v3/b.json");
        Check(b.has_value() && *b == Bytes("{}"), "past a file padded to two blocks, and without its \"./\"");

        Check(ExtractTarFile(tar, "deeply/nested/c.json") == Bytes("[]"), "a ustar prefix is part of the path");
        Check(!ExtractTarFile(tar, "v3/").has_value(), "a directory is not a file");
        Check(!ExtractTarFile(tar, "v3/missing.json").has_value(), "a missing file is nullopt, not an error");

        std::vector<uint8_t> corrupt = tar;
        corrupt[10] ^= 0x01;   // inside the first name: the checksum no longer adds up
        Check(Throws([&] { (void)ExtractTarFile(corrupt, "v3/b.json"); }), "a damaged header is rejected");

        std::vector<uint8_t> cut = Archive({ TarEntry("v3/a.json", long600) });
        cut.resize(700);   // the header says 600 bytes, 188 are there
        Check(Throws([&] { (void)ExtractTarFile(cut, "v3/a.json"); }), "a file running past the end is rejected");

        Check(Throws([&] { (void)ExtractTarFile(std::vector<uint8_t>(1024, 'x'), "v3/a.json"); }),
              "bytes that are not a ustar archive are rejected");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestDefinitionPath();
    TestBundle();
    TestTarReader();

    return TestResult();
}
