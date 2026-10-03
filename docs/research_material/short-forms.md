# Short forms: what every keycode prints

*Formalised 2026-10-03 with Rico.* What a key's legends say, for every named QMK keycode -- the
words, not their placement, which is in [ui-design.md](ui-design.md) ("Legends -- the plan" and
after). This is the table Nazg's legend set is written from: plain committed C++, by hand, like
the keycode and host-layout tables -- no generator in the repo. When QMK adds keycodes, extend it
by the rules below.

## How a legend is chosen

A legend never shrinks. The renderer tries, in order: the full form on one line, on two lines
(split at a space, or after the `_` of a QMK name), the short form on one line, on two, then cut
with "…". So short forms exist only where the full form fails on a 1u key.

**Checked.** Every entry below fits a 1u key at the smallest board size -- modifier text 9 px,
header 7.5 px (1/2 the letter size), 34 px of room -- in Arimo, at both families' weights, and
with Windows, Mac and Linux modifier names: 2,402 checks on one line, 166 on two, **none cut**.
Widths measured in the mockup's canvas, per character; Arimo applies no kerning to these strings.

## Kinds

| Kind | What it prints | Examples |
|---|---|---|
| **Standard key** | one legend, the keycap's own words; a short form where needed | Backspace (Bksp), Page Down (Pg Dn) |
| **Command** | a **header** -- what it acts on -- over a **main legend** -- the action | Media / Vol +, Light / Hue +, Hold / L2 |
| **Tap-hold** | the hold as header, over the tap's own legend | Ctrl / Esc, L1 / ! 1, Shift / ( |
| **Fallback** | QMK's own name, split after its prefix | MI_ / CHND -- MIDI and steno only |

Categories and colours are in ui-design.md: Behaviour, Host, Board, Firmware.

## The rules

**1. Header = what it acts on, main = the action.** "Haptic / Dwell +", "Mouse / Btn 1",
"Auto Shift / On/Off". A feature with no natural split takes a generic header: "Key / Leader",
"Key / Repeat", "Layer / Lock".

**2. The action vocabulary -- the same word for the same action everywhere.**

| QMK says | Nazg prints |
|---|---|
| On, Off | On, Off |
| Toggle *X* | On/Off, under *X*'s header |
| Up, Down -- on a quantity | +, − (the true minus) -- Vol +, Hue −, Dwell + |
| Up, Down -- on a direction | Up, Dn -- Wh Up, Pg Dn |
| Next, Previous -- cycling a list | Next, Prev -- Light / Next, Unicode / Prev |
| Reset | Reset |

**3. The word table** -- QMK's word, where too long for 1u:

| QMK | Nazg | | QMK | Nazg |
|---|---|---|---|---|
| Brightness, Value | Bri | | Button | Btn |
| Saturation | Sat | | Wheel | Wh |
| Speed | Spd | | Acceleration | Acc |
| Continuous | Cont | | Profile | Prof |
| Resolution | Res | | Feedback | Fdbk |
| Volume | Vol | | Frequency | Freq |
| Previous | Prev | | Tempo | BPM (short form) |

**4. Modifier names follow the *Modifier names* setting** (ui-design.md): `{GUI}` is Win, Cmd
or Super; `{ALT}` Alt or Option; right Alt Alt Gr or Option. Shortened in the tight places:
Ctrl to Ctl and Option to Opt in the Magic swaps' short headers, three modifiers to their
initials ("C A S+").

**5. Lighting.** One header for the four systems -- **Light** -- when the board has one; the
system's word when it has two: **Glow** (underglow), **Matrix** (RGB Matrix), **LEDs** (LED
Matrix), **Backlit** (backlight). The definition says which a board has (VIA's `keycodes`
modules and menus, Vial's `lighting`), never the keycode: `RGB_*` drove RGB Matrix on boards
without underglow, and `UG_*` still drives both by default (ui-design.md, "What a lighting
keycode drives"). The table notes the system each keycode is named for; a key driving every
system the board has says Light.

**6. Layers** -- the layer large, the operation as the header: Hold (MO), Toggle (TG), To (TO),
Once (OSL), Base (DF), **Set base** (PDF, which saves it), Tap tog (TT); LM is "Hold / L1 Ctrl".
A layer name, once layers have them, replaces "L1".

**7. Modified keys** -- `LCTL(KC_C)`: the modifiers as a header ending in "+", in the Host colour,
over the key's own legend: "Ctrl+ / C". The "+" and the colour set it apart from a tap-hold's
"Ctrl / C". Shift alone prints the shifted character instead ("!"); Hyper and Meh print their
names ("Hyper+", "Meh+"); three modifiers shorten to initials ("C A S+").

**8. Numbered families** print their number: Macro / M3, User / U3, Custom / KB 3 (a VIA
definition's `customKeycodes` name wins), Joystick / Btn 3, Prog btn / 3, Bluetooth / Prof 3,
Dance / TD 3.

**9. Fallback.** MIDI and steno -- nobody types them on a configurator board -- print QMK's own
name, split after its prefix where it does not fit ("MI_" / "CHND"). Hover gives the label.

**10. Hover always gives QMK's name and label**, so a short form may be terse.

## The table

Generated from QMK's keycode table (`NazgQmkKeycodeTable.cpp`, keycode spec 0.0.9) by the rules
above; short forms in parentheses. Names are the newest QMK spellings, which Nazg uses as
identities (keycodes.md).

### Standard keys

Letters, digits, punctuation, F1-F24 and the numpad print their QMK label, or what the host layout gives the position. The others:

| Keycode | QMK label | Legend (short) |
|---|---|---|
| `KC_BSPC` | Backspace | Backspace (Bksp) |
| `KC_SPC` | Spacebar | (blank) |
| `KC_CAPS` | Caps Lock | Caps Lock (Caps) |
| `KC_PSCR` | Print Screen | Print Screen (Prt Sc) |
| `KC_SCRL` | Scroll Lock | Scroll Lock (Scr Lk) |
| `KC_INS` | Insert | Insert (Ins) |
| `KC_PGUP` | Page Up | Page Up (Pg Up) |
| `KC_DEL` | Delete | Delete (Del) |
| `KC_PGDN` | Page Down | Page Down (Pg Dn) |
| `KC_NUM` | Num Lock | Num Lock (Num Lk) |
| `KC_APP` | App | Menu |
| `KC_EXEC` | Execute | Execute (Exec) |
| `KC_SLCT` | Select | Select (Sel) |
| `KC_KB_VOLUME_UP` | Volume Up | Vol + |
| `KC_KB_VOLUME_DOWN` | Volume Down | Vol − |
| `KC_LCAP` | Caps Lock | Lock Caps (L Caps) |
| `KC_LNUM` | Num Lock | Lock Num (L Num) |
| `KC_LSCR` | Scroll Lock | Lock Scroll (L Scr) |
| `KC_INT1` | INT 1 | Int 1 |
| `KC_INT2` | INT 2 | Int 2 |
| `KC_INT3` | INT 3 | Int 3 |
| `KC_INT4` | INT 4 | Int 4 |
| `KC_INT5` | INT 5 | Int 5 |
| `KC_INT6` | INT 6 | Int 6 |
| `KC_INT7` | INT 7 | Int 7 |
| `KC_INT8` | INT 8 | Int 8 |
| `KC_INT9` | INT 9 | Int 9 |
| `KC_LNG1` | LANG 1 | Lang 1 |
| `KC_LNG2` | LANG 2 | Lang 2 |
| `KC_LNG3` | LANG 3 | Lang 3 |
| `KC_LNG4` | LANG 4 | Lang 4 |
| `KC_LNG5` | LANG 5 | Lang 5 |
| `KC_LNG6` | LANG 6 | Lang 6 |
| `KC_LNG7` | LANG 7 | Lang 7 |
| `KC_LNG8` | LANG 8 | Lang 8 |
| `KC_LNG9` | LANG 9 | Lang 9 |
| `KC_ERAS` | Alternate Erase | Alt Erase (Erase) |
| `KC_SYRQ` | SysReq/Attention | SysRq |
| `KC_CNCL` | Cancel | Cancel (Cncl) |
| `KC_CLR` | Clear | Clear (Clr) |
| `KC_RETN` | Return | Return (Ret) |
| `KC_SEPR` | Separator | Separator (Sep) |
| `KC_CLAG` | Clear/Again | Clear Again (Clr Agn) |
| `KC_CRSL` | CrSel/Props | CrSel |
| `KC_LCTL` | Left Control | Control (Ctrl) |
| `KC_LSFT` | Left Shift | Shift |
| `KC_LALT` | Left Alt | {ALT} |
| `KC_LGUI` | Left GUI | {GUI} |
| `KC_RCTL` | Right Control | Control (Ctrl) |
| `KC_RSFT` | Right Shift | Shift |
| `KC_RALT` | Right Alt | {ALTGR} |
| `KC_RGUI` | Right GUI | {GUI} |

### Behaviour

| Keycode | QMK label | Header (short) | Main legend (short) |
|---|---|---|---|
| `AS_DOWN` | Auto Shift Down | Auto Shift (AutoSft) | Time − |
| `AS_OFF` | Auto Shift Off | Auto Shift (AutoSft) | Off |
| `AS_ON` | Auto Shift On | Auto Shift (AutoSft) | On |
| `AS_RPT` | Auto Shift Report | Auto Shift (AutoSft) | Report (Rpt) |
| `AS_TOGG` | Toggle Auto Shift | Auto Shift (AutoSft) | On/Off |
| `AS_UP` | Auto Shift Up | Auto Shift (AutoSft) | Time + |
| `AC_OFF` | Autocorrect Off | Autocorrect (AutoCor) | Off |
| `AC_ON` | Autocorrect On | Autocorrect (AutoCor) | On |
| `AC_TOGG` | Toggle Autocorrect | Autocorrect (AutoCor) | On/Off |
| `CW_TOGG` | Toggle Caps Word | Caps Word (Caps Wd) | On/Off |
| `CM_OFF` | Combo Off | Combos | Off |
| `CM_ON` | Combo On | Combos | On |
| `CM_TOGG` | Toggle Combo | Combos | On/Off |
| `QK_AREP` | Repeat Alternate | Key | Alt Rep |
| `QK_GESC` | Grave Esc | Key | Grv Esc |
| `QK_LEAD` | Leader | Key | Leader (Lead) |
| `QK_LOCK` | Lock | Key | Lock |
| `QK_REP` | Repeat | Key | Repeat (Rep) |
| `QK_LLCK` | Layer Lock | Layer | Lock |
| `OS_OFF` | One Shot Off | One shot | Off |
| `OS_ON` | One Shot On | One shot | On |
| `OS_TOGG` | Toggle One Shot | One shot | On/Off |
| `KO_OFF` | Key Overrides Off | Overrides (Ovrd) | Off |
| `KO_ON` | Key Overrides On | Overrides (Ovrd) | On |
| `KO_TOGG` | Toggle Key Overrides | Overrides (Ovrd) | On/Off |
| `SE_LOCK` | Lock | Secure | Lock |
| `SE_REQ` | Request Unlock | Secure | Request (Req) |
| `SE_TOGG` | Toggle Lock | Secure | On/Off |
| `SE_UNLK` | Unlock | Secure | Unlock |
| `SH_MOFF` | Swap Hands Momentary Off | Swap hands (Swap) | Hold off (Hold no) |
| `SH_MON` | Swap Hands Momentary On | Swap hands (Swap) | Hold |
| `SH_OFF` | Unswap Hands | Swap hands (Swap) | Off |
| `SH_ON` | Swap Hands | Swap hands (Swap) | On |
| `SH_OS` | Swap Hands One Shot | Swap hands (Swap) | Once |
| `SH_TOGG` | Toggle Swap Hands | Swap hands (Swap) | On/Off |
| `SH_TT` | Swap Hands Tap Toggle | Swap hands (Swap) | Tap tog |
| `DT_DOWN` | Dynamic Tapping Term Down | Tap term | − |
| `DT_PRNT` | Print Dynamic Tapping Term | Tap term | Print |
| `DT_UP` | Dynamic Tapping Term Up | Tap term | + |
| `TL_LOWR` | Lower | Tri layer | Lower |
| `TL_UPPR` | Upper | Tri layer | Upper |

### Host

| Keycode | QMK label | Header (short) | Main legend (short) |
|---|---|---|---|
| `KC_ASST` | Assistant | App | Assistant (Assist) |
| `KC_CALC` | Calculator | App | Calc |
| `KC_CPNL` | Control Panel | App | Settings (Setup) |
| `KC_LPAD` | Launchpad | App | Launchpad (Launch) |
| `KC_MAIL` | Mail | App | Mail |
| `KC_MCTL` | Mission Control | App | Mission Ctrl (Mission) |
| `KC_MYCM` | My Computer | App | Computer (PC) |
| `BT_NEXT` | Bluetooth Profile Next | Bluetooth (BT) | Next |
| `BT_PREV` | Bluetooth Profile Previous | Bluetooth (BT) | Prev |
| `BT_UNPR` | Unpair | Bluetooth (BT) | Unpair |
| `DM_PLY1` | Dynamic Macro Play 1 | Macro | Play 1 |
| `DM_PLY2` | Dynamic Macro Play 2 | Macro | Play 2 |
| `DM_REC1` | Dynamic Macro Record 1 | Macro | Rec 1 |
| `DM_REC2` | Dynamic Macro Record 2 | Macro | Rec 2 |
| `DM_RSTP` | Dynamic Macro Stop Recording | Macro | Stop |
| `KC_EJCT` | Eject | Media | Eject |
| `KC_MFFD` | Fast Forward | Media | Fast Fwd (FFwd) |
| `KC_MNXT` | Next | Media | Next |
| `KC_MPLY` | Play/Pause | Media | Play |
| `KC_MPRV` | Previous | Media | Prev |
| `KC_MRWD` | Rewind | Media | Rewind (Rew) |
| `KC_MSEL` | Media | Media | Select (Sel) |
| `KC_MSTP` | Stop | Media | Stop |
| `KC_MUTE` | Mute | Media | Mute |
| `KC_VOLD` | Volume Down | Media | Vol − |
| `KC_VOLU` | Volume Up | Media | Vol + |
| `MS_ACL0` | Acceleration 0 | Mouse | Acc 0 |
| `MS_ACL1` | Acceleration 1 | Mouse | Acc 1 |
| `MS_ACL2` | Acceleration 2 | Mouse | Acc 2 |
| `MS_BTN1` | Mouse Button 1 | Mouse | Btn 1 |
| `MS_BTN2` | Mouse Button 2 | Mouse | Btn 2 |
| `MS_BTN3` | Mouse Button 3 | Mouse | Btn 3 |
| `MS_BTN4` | Mouse Button 4 | Mouse | Btn 4 |
| `MS_BTN5` | Mouse Button 5 | Mouse | Btn 5 |
| `MS_BTN6` | Mouse Button 6 | Mouse | Btn 6 |
| `MS_BTN7` | Mouse Button 7 | Mouse | Btn 7 |
| `MS_BTN8` | Mouse Button 8 | Mouse | Btn 8 |
| `MS_DOWN` | Mouse Down | Mouse | Down |
| `MS_LEFT` | Mouse Left | Mouse | Left |
| `MS_RGHT` | Mouse Right | Mouse | Right |
| `MS_UP` | Mouse Up | Mouse | Up |
| `MS_WHLD` | Mouse Wheel Down | Mouse | Wh Dn |
| `MS_WHLL` | Mouse Wheel Left | Mouse | Wh Left (Wh Lt) |
| `MS_WHLR` | Mouse Wheel Right | Mouse | Wh Right (Wh Rt) |
| `MS_WHLU` | Mouse Wheel Up | Mouse | Wh Up |
| `OU_2P4G` | 2.4GHz | Output | 2.4 GHz (2.4G) |
| `OU_AUTO` | Output Auto | Output | Auto |
| `OU_BT` | Bluetooth | Output | BT |
| `OU_NEXT` | Next Output | Output | Next |
| `OU_NONE` | None | Output | None |
| `OU_PREV` | Previous Output | Output | Prev |
| `OU_USB` | USB | Output | USB |
| `KC_BRID` | Brightness Down | Screen | Bri − |
| `KC_BRIU` | Brightness Up | Screen | Bri + |
| `QK_STENO_BOLT` | -- | Steno | Bolt |
| `QK_STENO_COMB` | -- | Steno | Comb |
| `QK_STENO_COMB_MAX` | -- | Steno | Comb max |
| `QK_STENO_GEMINI` | -- | Steno | Gemini |
| `KC_PWR` | Power | System | Power |
| `KC_SLEP` | Sleep | System | Sleep |
| `KC_WAKE` | Wake | System | Wake |
| `UC_BSD` | Unicode Mode BSD | Unicode | BSD |
| `UC_EMAC` | Unicode Mode emacs | Unicode | Emacs |
| `UC_LINX` | Unicode Mode Linux | Unicode | Linux |
| `UC_MAC` | Unicode Mode macOS | Unicode | macOS |
| `UC_NEXT` | Unicode Mode Next | Unicode | Next |
| `UC_PREV` | Unicode Mode Previous | Unicode | Prev |
| `UC_WIN` | Unicode Mode Windows | Unicode | Windows (Win) |
| `UC_WINC` | Unicode Mode WinCompose | Unicode | WinCompose (WinC) |
| `KC_WBAK` | Back | Web | Back |
| `KC_WFAV` | Favorites | Web | Favorites (Favs) |
| `KC_WFWD` | Forward | Web | Forward (Fwd) |
| `KC_WHOM` | Home | Web | Home |
| `KC_WREF` | Refresh | Web | Refresh (Refr) |
| `KC_WSCH` | Search | Web | Search (Srch) |
| `KC_WSTP` | Stop | Web | Stop |
| `MC_n` (`MC_0`-`MC_31`) | Macro n | Macro | Mn |
| `JS_n` (`JS_0`-`JS_31`) | Button n | Joystick (Joy) | Btn n |
| `PB_n` (`PB_1`-`PB_32`) | Button n | Prog btn (Prog) | n |
| `BT_PRFn` (`BT_PRF1`-`BT_PRF5`) | Bluetooth Profile n | Bluetooth (BT) | Prof n |
| `QK_USER_n` (`QK_USER_0`-`QK_USER_31`) | User n | User | Un |

### Board

| Keycode | QMK label | Header (short) | Main legend (short) |
|---|---|---|---|
| `AU_NEXT` | Audio Voice Next | Audio | Voice + |
| `AU_OFF` | Audio Off | Audio | Off |
| `AU_ON` | Audio On | Audio | On |
| `AU_PREV` | Audio Voice Previous | Audio | Voice − |
| `AU_TOGG` | Toggle Audio | Audio | On/Off |
| `CL_CAPS` | Caps!=LCtl | Caps | Is Caps |
| `CL_CTRL` | Caps=LCtl | Caps | Is Ctrl |
| `CK_DOWN` | Audio Clicky Down | Clicky | Freq − |
| `CK_OFF` | Audio Clicky Off | Clicky | Off |
| `CK_ON` | Audio Clicky On | Clicky | On |
| `CK_RST` | Audio Clicky Reset | Clicky | Reset |
| `CK_TOGG` | Toggle Audio Clicky | Clicky | On/Off |
| `CK_UP` | Audio Clicky Up | Clicky | Freq + |
| `CL_NORM` | Unswap LCtl<->Caps | Ctrl↔Caps (Ctl/Caps) | Unswap |
| `CL_SWAP` | Swap LCtl<->Caps | Ctrl↔Caps (Ctl/Caps) | Swap |
| `CL_TOGG` | Toggle LCtl<->Caps | Ctrl↔Caps (Ctl/Caps) | On/Off |
| `CG_LNRM` | Unswap LCtl<->LGUI | Ctrl↔{GUI} (Ctl/{GUI}) | Unswap L |
| `CG_LSWP` | Swap LCtl<->LGUI | Ctrl↔{GUI} (Ctl/{GUI}) | Swap L |
| `CG_NORM` | Unswap Ctl<->GUI | Ctrl↔{GUI} (Ctl/{GUI}) | Unswap |
| `CG_RNRM` | Unswap RCtl<->RGUI | Ctrl↔{GUI} (Ctl/{GUI}) | Unswap R |
| `CG_RSWP` | Swap RCtl<->RGUI | Ctrl↔{GUI} (Ctl/{GUI}) | Swap R |
| `CG_SWAP` | Swap Ctl<->GUI | Ctrl↔{GUI} (Ctl/{GUI}) | Swap |
| `CG_TOGG` | Toggle Ctl<->GUI | Ctrl↔{GUI} (Ctl/{GUI}) | On/Off |
| `EH_LEFT` | EE Hands Left | EE Hands (Hands) | Left |
| `EH_RGHT` | EE Hands Right | EE Hands (Hands) | Right |
| `EC_NORM` | Unswap Esc<->Caps | Esc↔Caps (Esc/Caps) | Unswap |
| `EC_SWAP` | Swap Esc<->Caps | Esc↔Caps (Esc/Caps) | Swap |
| `EC_TOGG` | Toggle Esc<->Caps | Esc↔Caps (Esc/Caps) | On/Off |
| `HF_BUZZ` | Toggle Haptic Buzz | Haptic | Buzz |
| `HF_COND` | Haptic Continuous Down | Haptic | Cont − |
| `HF_CONT` | Toggle Haptic Continuous | Haptic | Cont |
| `HF_CONU` | Haptic Continuous Up | Haptic | Cont + |
| `HF_DWLD` | Haptic Dwell Down | Haptic | Dwell − |
| `HF_DWLU` | Haptic Dwell Up | Haptic | Dwell + |
| `HF_FDBK` | Toggle Haptic Feedback | Haptic | Fdbk |
| `HF_NEXT` | Haptic Mode Next | Haptic | Next |
| `HF_OFF` | Haptic Off | Haptic | Off |
| `HF_ON` | Haptic On | Haptic | On |
| `HF_PREV` | Haptic Mode Previous | Haptic | Prev |
| `HF_RST` | Haptic Reset | Haptic | Reset |
| `HF_TOGG` | Toggle Haptic | Haptic | On/Off |
| `BL_BRTG` | Toggle Breathing | Light -- Backlit | Breathe |
| `BL_DOWN` | Backlight Down | Light -- Backlit | Bri − |
| `BL_OFF` | Backlight Off | Light -- Backlit | Off |
| `BL_ON` | Backlight On | Light -- Backlit | On |
| `BL_STEP` | Backlight Step | Light -- Backlit | Step |
| `BL_TOGG` | Toggle Backlight | Light -- Backlit | On/Off |
| `BL_UP` | Backlight Up | Light -- Backlit | Bri + |
| `LM_BRID` | LED Matrix Brightness Down | Light -- LEDs | Bri − |
| `LM_BRIU` | LED Matrix Brightness Up | Light -- LEDs | Bri + |
| `LM_FLGN` | LED Matrix Flag Next | Light -- LEDs | Flag next |
| `LM_FLGP` | LED Matrix Flag Previous | Light -- LEDs | Flag prev |
| `LM_NEXT` | LED Matrix Next | Light -- LEDs | Next |
| `LM_OFF` | LED Matrix Off | Light -- LEDs | Off |
| `LM_ON` | LED Matrix On | Light -- LEDs | On |
| `LM_PREV` | LED Matrix Previous | Light -- LEDs | Prev |
| `LM_SPDD` | LED Matrix Speed Down | Light -- LEDs | Spd − |
| `LM_SPDU` | LED Matrix Speed Up | Light -- LEDs | Spd + |
| `LM_TOGG` | Toggle LED Matrix | Light -- LEDs | On/Off |
| `RGB_M_B` | -- | Light -- Glow | Breathe |
| `RGB_M_G` | -- | Light -- Glow | Gradient (Grad) |
| `RGB_M_K` | -- | Light -- Glow | Knight |
| `RGB_M_P` | -- | Light -- Glow | Plain |
| `RGB_M_R` | -- | Light -- Glow | Rainbow (Rainbw) |
| `RGB_M_SN` | -- | Light -- Glow | Snake |
| `RGB_M_SW` | -- | Light -- Glow | Swirl |
| `RGB_M_T` | -- | Light -- Glow | Test |
| `RGB_M_TW` | -- | Light -- Glow | Twinkle |
| `RGB_M_X` | -- | Light -- Glow | Xmas |
| `RM_FLGN` | RGB Matrix Flag Next | Light -- Matrix | Flag next |
| `RM_FLGP` | RGB Matrix Flag Previous | Light -- Matrix | Flag prev |
| `RM_HUED` | RGB Matrix Hue Down | Light -- Matrix | Hue − |
| `RM_HUEU` | RGB Matrix Hue Up | Light -- Matrix | Hue + |
| `RM_NEXT` | RGB Matrix Next | Light -- Matrix | Next |
| `RM_OFF` | RGB Matrix Off | Light -- Matrix | Off |
| `RM_ON` | RGB Matrix On | Light -- Matrix | On |
| `RM_PREV` | RGB Matrix Previous | Light -- Matrix | Prev |
| `RM_SATD` | RGB Matrix Saturation Down | Light -- Matrix | Sat − |
| `RM_SATU` | RGB Matrix Saturation Up | Light -- Matrix | Sat + |
| `RM_SPDD` | RGB Matrix Speed Down | Light -- Matrix | Spd − |
| `RM_SPDU` | RGB Matrix Speed Up | Light -- Matrix | Spd + |
| `RM_TOGG` | Toggle RGB Matrix | Light -- Matrix | On/Off |
| `RM_VALD` | RGB Matrix Value Down | Light -- Matrix | Bri − |
| `RM_VALU` | RGB Matrix Value Up | Light -- Matrix | Bri + |
| `UG_HUED` | RGB Underglow Hue Down | Light -- Glow | Hue − |
| `UG_HUEU` | RGB Underglow Hue Up | Light -- Glow | Hue + |
| `UG_NEXT` | RGB Underglow Next | Light -- Glow | Next |
| `UG_PREV` | RGB Underglow Previous | Light -- Glow | Prev |
| `UG_SATD` | RGB Underglow Saturation Down | Light -- Glow | Sat − |
| `UG_SATU` | RGB Underglow Saturation Up | Light -- Glow | Sat + |
| `UG_SPDD` | RGB Underglow Speed Down | Light -- Glow | Spd − |
| `UG_SPDU` | RGB Underglow Speed Up | Light -- Glow | Spd + |
| `UG_TOGG` | Toggle RGB Underglow | Light -- Glow | On/Off |
| `UG_VALD` | RGB Underglow Value Down | Light -- Glow | Bri − |
| `UG_VALU` | RGB Underglow Value Up | Light -- Glow | Bri + |
| `MU_NEXT` | Music Next | Music | Mode + |
| `MU_OFF` | Music Off | Music | Off |
| `MU_ON` | Music On | Music | On |
| `MU_TOGG` | Toggle Music | Music | On/Off |
| `NK_OFF` | NKRO Off | NKRO | Off |
| `NK_ON` | NKRO On | NKRO | On |
| `NK_TOGG` | Toggle NKRO | NKRO | On/Off |
| `SQ_OFF` | Sequencer Off | Seq | Off |
| `SQ_ON` | Sequencer On | Seq | On |
| `SQ_RESD` | Resolution Down | Seq | Res − |
| `SQ_RESU` | Resolution Up | Seq | Res + |
| `SQ_SALL` | All Steps | Seq | All |
| `SQ_SCLR` | Clear Steps | Seq | Clear |
| `SQ_TMPD` | Tempo Down | Seq | Tempo − (BPM −) |
| `SQ_TMPU` | Tempo Up | Seq | Tempo + (BPM +) |
| `SQ_TOGG` | Toggle Sequencer | Seq | On/Off |
| `VK_TOGG` | -- | Velocikey (VelKey) | On/Off |
| `BS_NORM` | Unswap \<->Bspc | \↔Bksp (\/Bksp) | Unswap |
| `BS_SWAP` | Swap \<->Bspc | \↔Bksp (\/Bksp) | Swap |
| `BS_TOGG` | Toggle \<->Bspc | \↔Bksp (\/Bksp) | On/Off |
| `GE_NORM` | Unswap \`<->Esc | \`↔Esc (\`/Esc) | Unswap |
| `GE_SWAP` | Swap \`<->Esc | \`↔Esc (\`/Esc) | Swap |
| `AG_LNRM` | Unswap LAlt<->LGUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | Unswap L |
| `AG_LSWP` | Swap LAlt<->LGUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | Swap L |
| `AG_NORM` | Unswap Alt<->GUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | Unswap |
| `AG_RNRM` | Unswap RAlt<->RGUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | Unswap R |
| `AG_RSWP` | Swap RAlt<->RGUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | Swap R |
| `AG_SWAP` | Swap Alt<->GUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | Swap |
| `AG_TOGG` | Toggle Alt<->GUI | {ALT}↔{GUI} ({ALTS}/{GUI}) | On/Off |
| `GU_OFF` | GUI Off | {GUI} key ({GUI}) | Off |
| `GU_ON` | GUI On | {GUI} key ({GUI}) | On |
| `GU_TOGG` | Toggle GUI | {GUI} key ({GUI}) | On/Off |
| `QK_KB_n` (`QK_KB_0`-`QK_KB_31`) | Keyboard n | Custom | KB n |

### Firmware

| Keycode | QMK label | Header (short) | Main legend (short) |
|---|---|---|---|
| `DB_TOGG` | Toggle Debug | Firmware | Debug |
| `EE_CLR` | Clear EEPROM | Firmware | Clear EE |
| `QK_BOOT` | Bootloader | Firmware | Boot |
| `QK_MAKE` | Make | Firmware | Make |
| `QK_RBT` | Reboot | Firmware | Reboot |

### Space Cadet: drawn as tap-hold keys

| Keycode | QMK label | Hold (header) | Tap |
|---|---|---|---|
| `SC_LSPO` | LSft/) | Shift | ( |
| `SC_RSPC` | RSft/) | Shift | ) |
| `SC_SENT` | RSft/Ent | Shift | Enter |
| `SC_LCPO` | LCtl/( | Ctrl | ( |
| `SC_RCPC` | RCtl/) | Ctrl | ) |
| `SC_LAPO` | LAlt/( | {ALT} | ( |
| `SC_RAPC` | RAlt/) | {ALTGR} | ) |
### Parameterised keycodes: built from their parts

| Keycode | Header | Main legend | Category |
|---|---|---|---|
| `MO(n)` | Hold | Ln | Behaviour |
| `TG(n)` | Toggle | Ln | Behaviour |
| `TO(n)` | To | Ln | Behaviour |
| `OSL(n)` | Once | Ln | Behaviour |
| `DF(n)` | Base | Ln | Behaviour |
| `PDF(n)` | Set base | Ln | Behaviour |
| `TT(n)` | Tap tog | Ln | Behaviour |
| `LM(n, mods)` | Hold | Ln *mods* -- "L1 Ctrl", "L1 C S" | Behaviour |
| `OSM(mods)` | Once | *mods* -- "Ctrl", "Ctrl Sft", "Hyper" | Behaviour |
| `TD(n)` | Dance | TD n -- or, with Vial's entries read, drawn as a tap-hold | Behaviour |
| `LT(n, kc)` | hold: Ln | the tap's own legend | Behaviour |
| `MT(mods, kc)` | hold: *mods* | the tap's own legend | Behaviour |
| `SH_T(kc)` | hold: Swap | the tap's own legend | Behaviour |
| `mods(kc)`, e.g. `LCTL(KC_C)` | *mods*+ -- "Ctrl+", "Ctrl Sft+" (C S+), "Hyper+", "Meh+" | the key's own legend | Host |
| `S(kc)` | -- | the shifted character the host layout gives | -- |
| Unknown value | -- | its hex value, "0x7E40" | -- |
