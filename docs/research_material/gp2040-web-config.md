# GP2040-CE Web Configurator — Findings

*Investigated 2026-09-20. Source tree: shallow clone of `OpenStickCommunity/GP2040-CE` at commit `4ee15e7` (2026-09-17).*

## Summary

GP2040-CE boots into a dedicated **web config mode** in which the RP2040 stops being a
gamepad and becomes a **USB network adapter**. The host sees a new Ethernet interface; the
device hands it an IP by DHCP, answers DNS and mDNS, and serves a React application over
HTTP at `http://192.168.7.1`.

It is the only configurator surveyed where **both** the UI and the device's self-description
live on the device itself.

## Architecture

```
React 18 + Vite + Bootstrap      www/src/  -> makefsdata.js -> C array in flash
        |
lwIP httpd (raw API)             lib/httpd/  + LWIP_HTTPD_CUSTOM_FILES
        |
lwIP TCP/IP, NO_SYS=1            lib/lwip-port/lwipopts.h - no RTOS, no sockets
        |
DHCP server + DNS + mDNS         lib/rndis/rndis.c:218  rndis_init()
        |
tud_network_recv_cb / xmit       TinyUSB common network glue
        |
RNDIS (or CDC-ECM / CDC-NCM)     src/drivers/net/NetDriver.cpp
        |
USB Full Speed, 12 Mbit/s
```

Entry point: `gp2040.cpp:261` calls `rndis_init(WEB_CONFIG_HOSTNAME)`.

## Network layer (`lib/rndis/rndis.c`, 259 lines)

| Item | Value |
|---|---|
| Device IP | `192.168.7.1 / 255.255.255.0`, no gateway |
| MAC | `02:02:84:6A:96:00` (`02` prefix = locally administered) |
| DHCP | On-device server hands the host a lease |
| DNS | On-device server; `dns_query_proc` resolves **every** name to `192.168.7.1` (captive-portal style) |
| mDNS | Responder registered so `<hostname>.local` resolves |
| HTTP | lwIP `httpd`, custom FS, web UI compiled into flash |

`lwipopts.h` notables: `NO_SYS=1` (bare metal), `LWIP_SOCKET=0`, `LWIP_NETCONN=0`
(raw API only), `LWIP_SINGLE_NETIF=1`, `TCP_MSS=1460`.

**Known wart:** `LWIP_HTTPD_SUPPORT_11_KEEPALIVE 0` with the in-source comment
*"Causes lockups with CGI requests."* HTTP/1.1 keep-alive is disabled to work around a bug,
so every request pays a connection teardown.

## The dual-configuration trick

`NetDriver.cpp:226` comments it directly: *Windows only works with RNDIS*. The firmware
exposes **two USB configurations** and lets the host pick:

- Config 1 -> **RNDIS** (Windows binds)
- Config 2 -> **CDC-ECM** (macOS and Linux bind)

A build flag would substitute CDC-NCM instead. The device descriptor uses
`TUSB_CLASS_MISC` / IAD (`NetDriver.cpp:138`) to avoid the classic RNDIS
"shows up as a COM port" misbinding.

## What RNDIS actually is

**Remote NDIS** — a Microsoft protocol from the Windows 2000/XP era. NDIS is the Windows
kernel's internal driver model for network cards. Rather than design a USB networking
protocol, Microsoft **serialized the NDIS driver interface itself** and shipped it over a bus.
The device pretends to be an NDIS miniport driver at the far end of a wire.

Wire layout:

| Pipe | Purpose |
|---|---|
| Control (EP0) | `SEND_ENCAPSULATED_COMMAND` / `GET_ENCAPSULATED_RESPONSE` |
| Interrupt IN | "a response is waiting" notification |
| Bulk IN / OUT | Ethernet frames, each in a 44-byte `RNDIS_PACKET_MSG` header |

Control messages: `RNDIS_INITIALIZE_MSG`, `QUERY_MSG` (OIDs), `SET_MSG`, `KEEPALIVE_MSG`,
`HALT_MSG`.

RNDIS has **no clean USB class code of its own** — it is normally declared as CDC
Communications (0x02) / subclass ACM (0x02) / protocol 0xFF, i.e. squatting in the
*modem* subclass. That is the root cause of its historical driver-binding problems.

## OS support matrix (verified against Microsoft's driver table)

| | Windows 10 | Windows 11 | macOS | Linux |
|---|---|---|---|---|
| **RNDIS** | yes (`Rndismp.sys`) | yes | **no, never** | yes (`rndis_host`) |
| **CDC-ECM** | **no, never** | **no, never** | yes | yes |
| **CDC-NCM** | **no** (officially) | yes (`UsbNcm.sys`) | yes | yes, since 2.6.38 (2011) |

**No single protocol covers all four columns.** That is why the dual configuration exists —
it is not a GP2040-CE limitation, it is the state of the world.

Microsoft's own table now says, in the RNDIS row: *"Microsoft recommends that hardware
vendors build USB NCM compatible devices instead. USB NCM is a public USB-IF protocol that
offers better throughput performance."* — yet it lists NCM as **Windows 11 / Server 2022
only**. `UsbNcm.sys` ships in some Windows 10 builds but does not auto-bind by compatible ID,
so making it work there needs a custom INF, which defeats the zero-install premise.

Note: RNDIS is **not** on Microsoft's formal deprecated-features list; the guidance is a
recommendation, not a deprecation. The stronger hostility is on the Linux side, where the
gadget driver carries security warnings and the host-side parsers have a CVE history.

Upstream TinyUSB has since made **CDC-NCM the default** for most MCUs, with dual RNDIS+ECM
as the legacy path. GP2040-CE sits on the old default.

## NCM path maturity in GP2040-CE — audited

**Verdict: unfinished scaffolding. It does not link, let alone run.**

1. **It does not build.** `headers/tusb_config.h:142` defines `CFG_TUD_ECM_RNDIS 1` and
   **never defines `CFG_TUD_NCM`**. TinyUSB defaults it to `0` (`tusb_option.h:453`), and all
   of `ncm_device.c` sits behind `#if (CFG_TUD_ENABLED && CFG_TUD_NCM)` (line 51). Setting
   `CFG_TUD_ECM_RNDIS 0` alone disables ECM/RNDIS *and leaves NCM off*, so `netd_init`,
   `netd_open`, `netd_reset`, `netd_control_xfer_cb` and `netd_xfer_cb` — all referenced by
   `NetDriver::initialize()` — have no definition. **Link error.** Both defines are required,
   and TinyUSB enforces mutual exclusion (`net_device.h:34`).
2. **One commit, ever.** `git log -S "NCM"` over `src/drivers/net/` and `headers/tusb_config.h`
   returns exactly `43358ed8` — *"GP2040-CE Input Driver system rehaul (#830)"*, 2024-02-07.
   It arrived as a side-effect of a large refactor and has never been revisited.
3. **Never compiled in CI.** `.github/workflows/cmake.yml` builds a ~60-board matrix, every
   one with the default flag. The `#else` branch has never seen a compiler in this repo.
4. **Tell-tale smell:** `NetDriver.cpp:230` hardcodes `configuration_arr[2]` in both modes, so
   in NCM mode element 1 is a stray `NULL`. Harmless (the `index < CONFIG_ID_COUNT` guard
   catches it) but nobody has built it and tidied up.

**What is correct:**

- `bNumConfigurations = CONFIG_ID_COUNT` properly collapses to 1
- `NCM_CONFIG_TOTAL_LEN` correctly uses `TUD_CDC_NCM_DESC_LEN`, not the RNDIS length
- The lwIP glue needs **zero** changes — `rndis.c` only uses `tud_network_recv_cb` /
  `tud_network_xmit_cb` / `tud_network_can_xmit` / `tud_network_recv_renew`, common to both
  TinyUSB net drivers. Only the filename is misleading.
- Their TinyUSB fork ships the modern buffered `ncm_device.c` with real NTB aggregation
- All NCM tunables have sane defaults: 3200-byte NTBs, 1 buffer each way, about **6.4 KB RAM**

Estimated effort to finish: two defines, delete the `[2]`, then test. About a day, dominated
by testing. Risk to watch: commit `07e42fa0` *"Fix for rndis crashing the i2c"* shows this
subsystem has had timing-interaction bugs; changing transports changes the USB interrupt and
buffering profile.

## Strengths

- **Zero-install, genuinely.** Inbox drivers on Windows and Linux. No download, no signing,
  no installer, nothing to maintain across three platforms.
- **A real browser as the UI toolkit** — hence React 18 + Bootstrap + i18next with 302 source
  files and translations. Works in **Firefox**, because plain HTTP uses no hardware API.
  This is the only browser-based configurator in this survey that is not Chromium-only.
- **A real IP stack** — duplex, multiplexed, flow-controlled, arbitrarily rich.
- **Point-to-point isolation.** The "network" is one USB cable; not routable, not on the LAN.
- **Self-contained.** DHCP + DNS + mDNS + HTTP on-device; the user plugs in and browses.

## Weaknesses

- **RNDIS security record.** The host-side parsers are kernel-mode code parsing
  attacker-controlled length fields from whatever you plugged in.
- **macOS does not support RNDIS at all** — hence the dual-config complexity.
- **Full Speed ceiling.** RP2040 USB is 12 Mbit/s; with bulk overhead and RNDIS's 44-byte
  per-packet header, realistically single-digit Mbit/s.
- **Flash and RAM cost.** lwIP + httpd + DHCP + DNS + mDNS + the entire built React app as a
  C array, on a part where flash is already contended.
- **Mode-exclusive.** Reboot out of gamepad mode to configure, and back again.
- **Fragile edges** — see the keep-alive lockup workaround.
- **DNS hijack is blunt.** Resolving every name to the device breaks general resolution if the
  OS prefers that interface's DNS.
- **Corporate IT hostility.** A USB device spawning a network adapter is the signature of
  malicious USB tooling; managed fleets frequently block it.
- **Hardcoded subnet** `192.168.7.0/24` can collide with a real LAN.

## Extending with custom features

GP2040-CE is not a third-party protocol — it is a single firmware whose UI it also owns.
"Extending" means modifying the whole vertical stack, which you can do because you control
all of it.

Adding a feature touches five layers:

| Layer | Work |
|---|---|
| Add-on | `src/addons/<name>.cpp` + `headers/addons/<name>.h`, subclassing `GPAddon` (`available` / `setup` / `preprocess` / `process` / `postprocess` / `reinit` / `name`) |
| Registration | `addons.LoadAddon(new X(), CORE0_Input)` — or `CORE1_Input` for display/LED/USB-host work |
| Config | new fields in `proto/config.proto`, defaults in `config_utils.cpp` |
| Web API | request handler in `webconfig.cpp` |
| UI | React page or component under `www/src` |
| Build | `CMakeLists.txt` entries for sources, libraries and include dirs |

The spec warns explicitly: **do not rename or reorder enumerations in `config.proto` or
`enums.proto`** — doing so breaks compatibility with previously saved configurations.

### The trade

**Unlimited expressiveness.** Because the device ships the renderer, the UI can be anything
you can write in React. No widget vocabulary, no typed-parameter scheme, no client to
negotiate with. This is the one system here where a custom feature can have a genuinely
bespoke interface.

**No third-party extension point.** There is no reserved namespace, no plugin mechanism, no
way for someone else to extend your firmware's configurator without modifying your firmware.
That is fine for GP2040-CE — it is one project, not a protocol — but it means the model
offers nothing to a designer who wants their boards configurable by a client they do not
also ship.

### Assessment

The arbitrary-code freedom is real and is exactly what the other four give up. It is also
precisely the property that makes the approach unsafe on a keyboard: a device that can ship
executable UI to a host is a code-execution channel. The constraint the others accept is
what makes them auditable.

## Why this does not transfer to keyboards

See [README.md](README.md) for the full argument. In brief:

1. **Security asymmetry.** A gamepad with a writable HTTP server is a curiosity. A *keyboard*
   with one is a keystroke-injection endpoint — any local process can reach `192.168.7.1`
   with no prompt and remap a key to a shell macro.
2. **Always-connected.** A keyboard is plugged into every machine you use, including managed
   work laptops. A permanent virtual NIC triggers VPN clients, firewall profiles, MDM/DLP.
3. **Boot context.** Keyboards must work in UEFI and through KVMs; gamepads need not.
4. **QMK has no path to it.** QMK's ARM/RP2040 stack is ChibiOS with its own USB layer
   (`tmk_core/protocol/chibios/usb_driver.c`), not TinyUSB. No USB-Ethernet class, no lwIP.
   Adding them means a long-lived fork — the very thing VIAL is criticised for.
5. **Flash floor.** The SAMD21 RNDIS reference implementation needs at least 128 KB flash and
   32 KB RAM before any UI assets. Fine on RP2040, impossible on STM32F103.

## Key files

| Path | Purpose |
|---|---|
| `src/drivers/net/NetDriver.cpp` | USB descriptors, RNDIS/ECM/NCM configuration selection |
| `lib/rndis/rndis.c` | lwIP glue, netif, DHCP/DNS/mDNS/httpd init |
| `lib/lwip-port/lwipopts.h` | lwIP tuning |
| `lib/httpd/` | Custom filesystem for the embedded web UI |
| `headers/tusb_config.h` | TinyUSB class configuration (`CFG_TUD_ECM_RNDIS`) |
| `src/webconfig.cpp` | 3376 lines — the HTTP request handlers |
| `www/` | React 18 + Vite source for the config UI |

## Sources

- <https://gp2040-ce.info/>
- [USB Device Class Drivers Included in Windows — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/supported-usb-classes)
- [USB CDC NCM devices not assigned to UsbNcm driver automatically — Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/52386/usb-cdc-ncm-devices-not-assigned-to-usbncm-driver)
- [CONFIG_USB_NET_CDC_NCM — Linux Kernel Driver DataBase](https://cateee.net/lkddb/web-lkddb/USB_NET_CDC_NCM.html)
- [iOS: a journey in the USB networking stack — Synacktiv](https://www.synacktiv.com/en/publications/ios-a-journey-in-the-usb-networking-stack)
- [majbthrd/D21rndis — SAMD21 RNDIS embedded web server](https://github.com/majbthrd/D21rndis)
- [tinyusb net_lwip_webserver example](https://github.com/hathach/tinyusb/tree/master/examples/device/net_lwip_webserver)
