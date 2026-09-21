# ZMK Studio — Findings

*Investigated 2026-09-20 against zmk.dev documentation and the `zmkfirmware/zmk-studio-messages`
protobuf definitions.*

## Summary

**ZMK Studio** provides runtime keymap editing for ZMK-powered keyboards — change layers and
key assignments on a running keyboard with no reflash. MVP went GA in **November 2024**.

It is the most complete shipping realisation of "self-describing device + already-installed
renderer", and is architecturally the closest thing to an answer for the goals in
[README.md](README.md).

## Transport

Two transports, both driverless:

| Transport | Details |
|---|---|
| **USB** | A plain serial/UART transport over the **CDC-ACM** class, enabled by the `studio-rpc-usb-uart` snippet |
| **BLE** | Custom GATT service, UUID `00000000-0196-6107-c967-c5cfb1c2482a`, one characteristic (`...0001...`); client writes in, device responds via GATT **Indications** |

CDC-ACM binds driverlessly on Windows (10 **and** 11, unlike CDC-NCM), macOS and Linux. No
network adapter, no INF, no IT-policy trigger.

## Wire format

**Protocol Buffers v3** with simple byte framing:

| Byte | Meaning |
|---|---|
| `0xAB` | Start of Frame |
| `0xAC` | Escape |
| `0xAD` | End of Frame |

Any of those three appearing inside the payload is prefixed with the Escape byte.

### Message model

Three message types:

- **Request** — client to device
- **RequestResponse** — device's direct reply
- **Notification** — **unsolicited** device-to-client messages (lock state changes, unsaved
  changes status)

The notification channel matters: the device can push state, not merely answer queries.

## Self-description — the standout feature

Definitions live in [`zmkfirmware/zmk-studio-messages`](https://github.com/zmkfirmware/zmk-studio-messages),
across `core.proto`, `keymap.proto`, `behaviors.proto`, `meta.proto`, `studio.proto`.

### Physical layout — geometry from the device

```protobuf
message PhysicalLayouts {
    uint32 active_layout_index = 1;
    repeated PhysicalLayout layouts = 2;
}
message PhysicalLayout {
    string name = 1;
    repeated KeyPhysicalAttrs keys = 2;
}
message KeyPhysicalAttrs {
    sint32 width = 1;   sint32 height = 2;
    sint32 x = 3;       sint32 y = 4;
    sint32 r = 5;       // rotation
    sint32 rx = 6;      sint32 ry = 7;   // rotation origin
}
```

Full KLE-class geometry **including rotation and rotation origin**, delivered by the device.
The client draws an accurate graphical keyboard — rotated thumb clusters on split boards
included — with no registry and no side-loaded JSON.

Note `PhysicalLayouts` is plural with an `active_layout_index`, and
`set_active_physical_layout` switches at runtime. One PCB can declare several plate options
natively.

### Behaviours — a typed parameter vocabulary

```protobuf
message GetBehaviorDetailsResponse {
    uint32 id = 1;
    string display_name = 2;
    repeated BehaviorBindingParametersSet metadata = 3;
}
message BehaviorParameterValueDescription {
    string name = 1;
    oneof value_type {
        BehaviorParameterNil        nil = 2;
        uint32                      constant = 3;
        BehaviorParameterValueDescriptionRange range = 4;   // {min, max}
        BehaviorParameterHidUsage   hid_usage = 5;          // {keyboard_max, consumer_max}
        BehaviorParameterLayerId    layer_id = 6;
    }
}
```

The device enumerates its behaviours **and describes each parameter's type**. A client can
render a sensible editor for a behaviour it has never heard of:

- `range` -> a slider bounded by min/max
- `layer_id` -> a layer picker
- `hid_usage` -> a keycode picker
- `constant` / `nil` -> fixed or absent

**This is the key design insight in the whole survey.** Bespoke UI does not require the device
to ship arbitrary *code* — it requires a well-typed parameter *vocabulary*. That keeps the
richness while staying auditable and safe, unlike an embedded HTTP server shipping arbitrary
JavaScript.

### Keymap model

```protobuf
message Keymap {
    repeated Layer layers = 1;
    uint32 available_layers = 2;
    uint32 max_layer_name_length = 3;
}
message Layer  { uint32 id; string name; repeated BehaviorBinding bindings; }
message BehaviorBinding { sint32 behavior_id; uint32 param1; uint32 param2; }
```

Operations include `set_layer_binding`, `add_layer`, `remove_layer`, `move_layer`,
`restore_layer`, `set_layer_props` (rename), plus explicit
`check_unsaved_changes` / `save_changes` / `discard_changes` with typed error codes
(`NO_SPACE`, `NOT_SUPPORTED`, `INVALID_LOCATION`, ...).

### Core

`GetDeviceInfoResponse { string name; bytes serial_number; }`, lock state queries, and
`reset_settings`.

## Security — mandatory physical unlock

The keyboard ships **locked**. Nothing can be modified until the user physically presses a
key bound to **`&studio_unlock`**. Lock state is queryable (`get_lock_state`), settable
(`lock`), and changes arrive as `lock_state_changed` notifications.

Unlike VIAL's optional unlock, ZMK's is **mandatory** — a deliberate choice for a firmware
shipped to strangers.

Comparison across the survey:

| | Physical unlock |
|---|---|
| VIA | no |
| VIAL | yes (optional, `VIAL_INSECURE` disables) |
| XAP | yes (secure routes + unlock sequence) |
| **ZMK Studio** | **yes, mandatory** |

## Client

- **Native desktop app** for Windows, macOS and Linux, built with **Tauri**
- **Web version** for Chrome/Edge

Crucially, the native app uses **native serial, not WebSerial**. The Chromium-only problem is
entirely avoidable — use the desktop app and never touch a browser. This is the
"ship the renderer once" model in production.

## Requirements and limitations

### Build requirements

- `ZMK_STUDIO` Kconfig enabled
- The `studio-rpc-usb-uart` snippet included
- Physical layout must define the `keys` property
- Must **not** have a `chosen zmk,matrix-transform`
- Must support CDC-ACM console snippets
- Split keyboards: configure the central/left side only
- **Significantly more RAM**; some MCUs need tuning

### What it can change

- Key assignments on existing layers, while the keyboard is in use
- Predefined and user-defined behaviours (those already in devicetree)
- Switching between declared alternative physical layouts
- Renaming layers and enabling extra (pre-allocated) layers

### What it cannot change

- New behaviour definitions not present in devicetree
- Additional layers beyond the devicetree allocation
- New physical layouts
- Encoder assignments (explicitly low priority)

**It is runtime *assignment*, not runtime *definition*.** Adding a new behaviour or an extra
layer still requires a rebuild.

### The footgun

Once you have used ZMK Studio, **changes to your `.keymap` file stop applying** until you
perform "Restore Stock Settings" in the UI. Settings in flash shadow the compiled keymap.
This surprises people.

## Extending with custom features

ZMK's answer splits sharply depending on *what kind* of thing you are adding: extending the
behaviour vocabulary is free and elegant; extending the protocol is impossible without a fork.

### Case A — a custom behaviour (fully supported, best story of the four)

Write the behaviour the normal ZMK way:

| File | Purpose |
|---|---|
| `dts/bindings/behaviors/zmk,behavior-<name>.yaml` | binding; includes `zero_param` / `one_param` / `two_param` |
| `src/behaviors/behavior_<name>.c` | driver, with `.binding_pressed` / `.binding_released` |
| `Kconfig` | `ZMK_BEHAVIOR_<NAME>`, depends on `DT_HAS_ZMK_BEHAVIOR_<NAME>_ENABLED` |
| `CMakeLists.txt` | conditional source inclusion, respecting split locality |
| `dts/behaviors/<name>.dtsi` *(optional)* | a predefined instance, like `&mt` |

Then two extra things make it visible in Studio. First, `display-name` in the devicetree node
(the `behavior-metadata.yaml` binding contributes exactly this one property):

```dts
kp: key_press {
    compatible = "zmk,behavior-key-press";
    #binding-cells = <1>;
    display-name = "Key Press";
};
```

Second, parameter metadata in C, guarded by `CONFIG_ZMK_BEHAVIOR_METADATA`:

```c
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata param_values[] = {
    { .display_name = "Key", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE },
};
static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values     = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};
static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets     = param_metadata_set,
};
#endif
```

attached via `.parameter_metadata = &metadata` in the driver API, or
`.get_parameter_metadata` for dynamic cases.

**Result: the stock ZMK Studio client renders an editor for your behaviour with no client
work whatsoever.** That is the typed-parameter vocabulary paying off.

Note `param_metadata_set` is an *array* of sets, so param2's valid options can depend on
param1 and the UI shows the appropriate input for the current selection.

### Case B — anything that is not a behaviour (no extension point)

Custom RGB control, a config subsystem, your own device commands. `studio.proto` is a closed
enumeration:

```protobuf
message Request {
    uint32 request_id = 1;
    oneof subsystem {
        zmk.core.Request      core      = 3;
        zmk.behaviors.Request behaviors = 4;
        zmk.keymap.Request    keymap    = 5;
    }
}
```

No vendor range, no keyboard/user subsystem, **no reserved field numbers for third parties**.
Adding one means modifying `zmk-studio-messages` (the shared repo), the firmware RPC
handlers *and* the client — i.e. forking the shared message definitions. Precisely the VIAL
situation.

### Assessment

Within its chosen scope ZMK is the **best of the four for generic third-party clients**: a
designer adds a behaviour and every stock client renders it correctly, having never heard of
it. Outside that scope there is no path at all short of a fork.

This is the exact mirror image of [XAP](xap.md#extending-with-custom-features), which has a
first-class protocol extension mechanism but no semantic vocabulary for clients to render
from.

## Strengths

- **Fully self-describing** — geometry *and* typed behaviour parameters. No registry, no
  side-loaded files, no skew.
- **Typed parameter vocabulary** — the client can render editors for behaviours it does not
  know about. Nothing else in the survey does this.
- **Mandatory physical unlock** — best security posture of the four.
- **Driverless on every OS, including Windows 10** — CDC-ACM, unlike CDC-NCM.
- **Two transports**, including BLE, which matters for wireless boards.
- **Native app avoids the browser question entirely** — Firefox objection does not arise.
- **Upstream and official.** No fork.
- **Multiple physical layouts per board**, switchable at runtime.
- **Notification channel** for device-initiated state changes.
- **Bulk-efficient encoding** — protobuf over a serial stream, no 32-byte packet quantisation.

## Weaknesses

- **ZMK, not QMK.** Zephyr, devicetree, a different ecosystem. Not portable to QMK boards.
- **Bounded by devicetree** — assignment only, not definition.
- **Keymap-focused.** No RGB, no audio, no lighting subsystems in the protocol yet; encoders
  explicitly deprioritised. Narrower runtime scope than XAP's design.
- **RAM-hungry**, with documented MCU tuning requirements.
- **The "Restore Stock Settings" footgun.**
- **Constrained build configuration** — no matrix transform, central-side only for splits.

## Third-party implementation is viable

- [SergioRibera/zmk-protocol](https://github.com/SergioRibera/zmk-protocol) — maintained Rust
  library of the protobuf v3 message definitions
- At least one independent desktop app speaking Studio RPC over USB and BLE

The protocol is documented, the message definitions are public, and the framing is trivial.
A third-party client is genuinely implementable.

## Relevance to a multi-protocol client

**Build the internal model around ZMK Studio's shape.** It is the most expressive of the four:
physical layout as geometry, behaviours as typed-parameter descriptors, layers as
first-class objects with explicit save/discard semantics.

VIA and VIAL then become **lossy projections** onto that model, which is the tractable
direction. Starting from VIA's model and stretching it to fit ZMK Studio would be painful.

## Sources

- [ZMK Studio — ZMK Firmware docs](https://zmk.dev/docs/features/studio)
- [ZMK Studio RPC Protocol](https://zmk.dev/docs/development/studio-rpc-protocol)
- [zmkfirmware/zmk-studio-messages](https://github.com/zmkfirmware/zmk-studio-messages)
- [ZMK Studio MVP General Availability (2024-11-11)](https://zmk.dev/blog/2024/11/11/zmk-studio-mvp-ga)
- [zmkfirmware/zmk-studio](https://github.com/zmkfirmware/zmk-studio)
- [SergioRibera/zmk-protocol](https://github.com/SergioRibera/zmk-protocol)
