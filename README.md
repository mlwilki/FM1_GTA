# FM1_GTA (Out on Parole — M-VAVE FM-1 Edition)

A clean, native open-world crime game firmware for the **M-VAVE FM-1** (`foma` platform, identity `FM-1_187`), ported from [mlwilki/outonparole](https://github.com/mlwilki/outonparole).

**Play & Install in Browser:** https://mlwilki.github.io/FM1_GTA/#emulator

## Features
- **2.5D Top-Down Open City (240×240 RGB565):** Extruded rooftops, 4-level camera zoom (`CLOSE` / `NORMAL` / `WIDE` / `SATELLITE`), traffic lights, crosswalks, Harbor Row docks, Market Row, Pawn Kings, Respray Garage, Hospital, Police Station, coastline, and a 24-hour day/night cycle with headlights and lit windows.
- **3D-Style Perspective Cutscenes:** Street-level 3D-projected animations for **carjacking** (swinging door, driver yanked onto the asphalt) and **getting busted** (police cruiser, flashing red/blue lightbar beams, kneeling hands-up arrest & handcuffs).
- **5 Story Missions from Walt:** *Day Three*, *Dale's Wagon*, *Spin Cycle*, *Export Quality*, and *Ray Runs*.
- **Side Activities:** Pawn Kings gun shop, Respray heat-clearing garage, Docks Car Export wanted list, Taxi fares with tips, Laundromat store robberies, and 16 hidden cash stashes.
- **10 Vehicle Models & 4 Weapons:** Sedan, Compact, Hatch, Minivan, Pickup, SUV, Wagon, Offroad, Cop Cruiser, and Taxi; Fist, Pistol, SMG, and Shotgun.
- **5-Star Police & Helicopter System:** Escalating cruisers, on-foot cops, overhead helicopter spotlight, evasion cooldown, and pulsing red/blue police lightbar across the 27 FM-1 key LEDs.
- **Pure Integer 44.1 kHz Synth Audio & GTA-Style FM Radio:** Synthesized 4/6/8-cylinder engines, wail sirens, tire screech, gunshots, explosions, and 4 multi-track FM synth radio stations (*Vice Wave FM*, *West Coast FM*, *Liberty Noir*, *Parole Electro*) plus *PD Scanner*.
- **Flash Save:** Save your cash, missions, weapons, zoom level, and stashes to FM-1 flash.

## FM-1 Controls

| Action | FM-1 Hardware | Browser Keyboard |
|---|---|---|
| Move / Drive | Left keys (`F3` Left, `G#3` Up/Gas, `G3` Down/Brake, `A3` Right) or `OCT-`/`OCT+` | `W A S D` or Arrow Keys |
| Fire / Punch | `PLAY` button (or top key `G5`) | `Space` |
| Enter / Exit Car / Buy / Interact | `REC` button (or key `F5`) | `F` or `Enter` |
| Sprint / Handbrake Drift | `ARP` button | `Shift` |
| Reload / Horn / Cop Siren | `SEQ` button | `R` / `H` |
| Prev / Next Weapon (or Shop Item) | `FX` / `SEL` buttons (or `KNOB1`) | `Q` / `E` |
| Camera Zoom Out / In | `ENV` / `LFO` buttons (or `KNOB2`) | `-` / `=` |
| Full City Map | `HOME` button | `M` |
| Pause / Status | `EDIT` button | `P` or `Escape` |
| Cycle FM Radio Station | `GLO` button (or `PRESETS` knob) | `Tab` |
| Save Progress | `SAVE` button | Click `SAVE` |

## Build & Flash

```bash
./fm1 dev     # Live browser emulator & web installer at http://localhost:8000
./fm1 build   # Compiles build/web/firmware.wasm and build/fm1-gta-0.7.fwsc
./fm1 flash   # Flashes build/web/firmware.fwsc over USB to a connected FM-1
```

## License
GPL-3.0-only. Built on the `foma` / Felucca / SLOOP / Dinghy FM-1 toolchain.
