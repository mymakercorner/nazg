# Upstream reports

Bugs found in other projects while building Nazg, each with a report drafted, ready to file.
Kept here so that the Vial people -- and others -- can be contacted when the time comes. A report
filed gets its link and date in the table; one fixed upstream gets the commit.

Each draft was checked against the upstream default branch on the date given; check again before
filing, the bug may have gone.

| # | Project | What | Found | Checked upstream | Filed |
|---|---|---|---|---|---|
| 1 | vial-kb/vial-qmk | Alt Repeat sends right-hand modifiers wrong | 2026-10-10 | 2026-10-10, `vial` branch: present, no issue | no |
| 2 | vial-kb/vial-gui | Key override options 4 and 5 have each other's label | 2026-10-10 | 2026-10-10, `main`: present | no |
| 3 | llvm/llvm-project (WebAssembly) | `-fwasm-exceptions`: invalid module for a catch around a `co_await` in a loop | 2026-10-24 | Emscripten 6.0.13 and 5.0.7: present; the tracker not searched yet | no |

---

## 1. vial-qmk: Alt Repeat sends right-hand modifiers wrong

Found reading `quantum/vial.c` for Nazg's Alt Repeat Key section (ui-design.md, "The Alt Repeat
Key section"); never seen on hardware -- no board at hand has alt repeat slots. The arithmetic
below is from the source, `quantum/keycodes.h` and `quantum/action_code.h`. Before filing, it
would be worth seeing it once on a board: a Model F rebuilt on current vial-qmk would do.

File at <https://github.com/vial-kb/vial-qmk/issues/new>. The feature came with #906 (Pascal
Getreuer, @getreuer, who also wrote QMK's Repeat Key) -- mention him.

**Title:** Alt Repeat Key: right-hand modifiers in the alt key are sent wrong

**Body:**

```markdown
**Summary**

In `get_alt_repeat_key_keycode_user()` (`quantum/vial.c`, added in #906), the keycode Alt Repeat
sends is built as `(mods << 8) | key` with an **8-bit** modifier mask, where a `QK_MODS` keycode
holds a **5-bit** one. Left-hand modifiers happen to have the same value in both forms, so they
work. Right-hand modifiers do not: the result is another keycode altogether.

**Where**

`alt_repeat_key_normalize_keycode()` unpacks the 5-bit modifiers of an entry's keycode into
8 bits with `unpack_mods5()`, and the three lines building what is sent shift those 8 bits back
into the keycode as they are:

    alt_keycode = (entry->alt_required_mods << 8) | entry->alt_keycode;  // match
    alt_keycode = (entry->required_mods << 8) | entry->keycode;          // bidirectional
    alt_keycode = (entry->alt_required_mods << 8) | entry->alt_keycode;  // default

**What is sent**

An entry with alt key `RCTL(KC_Y)` (0x111C): `unpack_mods5(0x11)` gives 0x10 (Right Ctrl, 8-bit),
and `(0x10 << 8) | KC_Y` is 0x101C -- `QK_MODS` with only the right-hand flag set and no modifier:
**plain Y**.

| Alt key | 8-bit mods | Built | Which is |
|---|---|---|---|
| `LCTL(KC_Y)` | 0x01 | 0x011C | `LCTL(KC_Y)` -- correct |
| `RCTL(KC_Y)` | 0x10 | 0x101C | `KC_Y` with no modifier |
| `RSFT(KC_Y)` | 0x20 | 0x201C | in `QK_MOD_TAP`: a mod-tap of Y with no modifier |
| `RALT(KC_Y)` | 0x40 | 0x401C | in `QK_LAYER_TAP`: `LT(0, KC_Y)` |
| `RGUI(KC_Y)` | 0x80 | 0x801C | in `QK_UNICODEMAP` |

The same happens on the way back with a bidirectional entry whose *last* key has right-hand
modifiers, and with a default entry. Matching is not affected: `alt_repeat_key_mods_match()`
compares 8-bit masks with 8-bit masks, as it should.

QMK's own path in `quantum/repeat_key.c`, `get_alt_repeat_key_keycode()`, converts to 5 bits
before building the keycode ("Convert 8-bit mods to the 5-bit format used in keycodes"), so the
built-in pairs are fine.

**Suggested fix**

Pack the mods back to 5 bits where the keycode is built -- the inverse of `unpack_mods5()`. The
masks come from a single keycode's 5-bit mods, so they are all-left or all-right and the round
trip is exact:

    // 8-bit mods back to the 5-bit form of a QK_MODS keycode -- the inverse of unpack_mods5().
    static uint8_t pack_mods5(uint8_t mods8) {
        return (mods8 & 0xf0) != 0 ? (0x10 | (mods8 >> 4)) : mods8;
    }

    alt_keycode = (pack_mods5(entry->alt_required_mods) << 8) | entry->alt_keycode;
    alt_keycode = (pack_mods5(entry->required_mods) << 8) | entry->keycode;
    alt_keycode = (pack_mods5(entry->alt_required_mods) << 8) | entry->alt_keycode;

**Workaround until then:** use left-hand modifiers in alt keys, and in the last key of a
bidirectional entry (with "Ignore mod handedness" if the right-hand one should still match).
```

What Nazg does meanwhile: says so under the rule and offers "Use the left one"
(`ui/NazgAltRepeatKeySection.cpp`, `SentWrong()` in `model/NazgAltRepeatKey.h`). When vial-qmk
is fixed, that warning should only show for firmware older than the fix -- which no client can
tell, since alt repeat came with no protocol bump.

---

## 2. vial-gui: key override options 4 and 5 have each other's label

Found reading vial-gui for Nazg's Key Overrides section (ui-design.md, "The Key Overrides
section"). In `src/main/python/editor/key_override.py`, `OptionsUI`:

File at <https://github.com/vial-kb/vial-gui/issues/new>.

**Title:** Key Overrides: the labels of "no reregister trigger" and "no unregister on other key down" are swapped

**Body:**

```markdown
In `editor/key_override.py`, `OptionsUI`, the two last option checkboxes carry each other's
label:

    self.opt_no_reregister_trigger = CheckBoxNoPadding("Don't deactivate when another key is pressed down")
    self.opt_no_unregister_on_other_key_down = CheckBoxNoPadding(
        "Don't register the trigger key again after the override is deactivated")

`no_reregister_trigger` is bit 4 (`ko_option_no_reregister_trigger`): after the override ends,
the trigger key still held is not registered again. `no_unregister_on_other_key_down` is bit 5
(`ko_option_no_unregister_on_other_key_down`): pressing another key does not end the override
(QMK `quantum/process_keycode/process_key_override.h`). `protocol/key_override.py` maps the bits
correctly; only the labels are swapped, so ticking "Don't deactivate when another key is pressed
down" sets bit 4.

Fix: swap the two strings.
```

What Nazg does: its own words for both, the two bits shown turned round so every box ticked is
QMK's usual way (`ui/NazgKeyOverridesSection.cpp`, `DrawOptions()`).

---

## 3. LLVM WebAssembly: a catch around a `co_await` in a loop gives an invalid module

Found building Nazg's web build (client-architecture.md, "Exceptions, and the frame loop"): in
Nazg's own code the exception escaped the coroutine instead of being caught; reduced, the module
is rejected outright. Search <https://github.com/llvm/llvm-project/issues> (label
`backend:WebAssembly`, "coroutine" and "exception") before filing; if nothing, file there, or at
<https://github.com/emscripten-core/emscripten/issues> if Emscripten is thought the better door.

**Title:** [WebAssembly] -fwasm-exceptions: invalid module ("catch does not match a try") for a try/catch around co_await in a loop

**Body:**

````markdown
**Summary**

With `-fwasm-exceptions`, a coroutine that catches, inside a loop, what an awaiter with a
non-trivial destructor throws from `await_resume()` compiles to an invalid module from `-O1` on.
With the legacy encoding V8 rejects it ("catch does not match a try") and `wasm-opt` fails to
parse it at `-O2`/`-O3`; with exnref (`-sWASM_LEGACY_EXCEPTIONS=0`) it is invalid even at `-O0`.
In a larger program the module validated but the catch missed the exception, which escaped the
coroutine's `resume()`. `-fexceptions` (Emscripten's JavaScript-based EH) is correct at every level.

**Reproduction** (Emscripten 6.0.13, clang 24.0.0git 83ee07799c34; also 5.0.7, clang 23.0.0git)

```cpp
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

struct Task {
    struct promise_type {
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::printf("unhandled_exception\n"); }
    };
    std::coroutine_handle<promise_type> handle;
};

std::coroutine_handle<> g_waiting;

struct Failing {
    std::shared_ptr<int> state = std::make_shared<int>(1);   // a destructor to run when it throws
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { g_waiting = h; }
    void await_resume() { throw std::runtime_error("failed"); }
};

int g_caught = 0;

Task Looping(int count) {
    for (int i = 0; i < count; ++i) {
        try { co_await Failing{}; }
        catch (const std::runtime_error&) { ++g_caught; }
    }
}

int main() {
    Task t = Looping(3);
    for (int i = 0; i < 3; ++i) g_waiting.resume();
    std::printf("caught %d, done %d\n", g_caught, t.handle.done() ? 1 : 0);
    std::_Exit(0);
}
```

```
em++ -std=c++20 -O1 -fwasm-exceptions repro.cpp -o repro.js && node repro.js
```

Expected `caught 3, done 1` (what `-O0` and `-fexceptions` print). Got:

```
CompileError: WebAssembly.instantiate(): Compiling function #7 failed: catch does not match a try @+1193
```

At `-O2`: `wasm-opt` fails, `[parse exception: unexpected catch]`. With
`-sWASM_LEGACY_EXCEPTIONS=0 -O0`: `expected 1 elements on the stack for branch, found 0`.

Without the loop, or with a trivially destructible awaiter, it is fine.
````

What Nazg does: `-fexceptions` in the web build (CMakeLists.txt).
