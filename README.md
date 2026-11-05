# TDU_DealerOwnedGreen

ASI plugin for **Test Drive Unlimited** (PC, `TestDriveUnlimited.exe` 1.66A,
sha256 `1408778de813902587ad83438bce52a606f8895c5d74478762ad1c9cf92d5a96`).

At a car or bike dealer, the name of any vehicle you already own is shown in **green**
(colour configurable). The game's own colours still apply to everything you don't own:
red = out of stock at this dealer, blue = reserved.

## How it works

* The dealer UI is the Flash movie `FrontEnd/{HiRes,LowRes}/shops.bnk`. Its `infos` clip has
  frames `default` / `red` / `blue` / `available`, each with its own `/:Current_car` EditText
  (chars `0x49/0x4D/0x55/0x59`) with a fixed colour; the frame is chosen from `/:IsCarInStock`,
  set by the exe in `DealerSelectCar` (`0x726110`).
* The engine's EditText renderer (`0x44BE60` -> `0x99EC00`) reads the colour from the EditText
  definition (`def+0x20`, RGBA) on every draw.
* The plugin redirects `call 0x7782A0` at `0x7264AF` (right after the selected vehicle id is
  stored in `dealer+0x107C44`; ECX = vehicle id). The wrapper preserves all registers and
  checks the player's garage: profile = `[0x114C15C]->vtbl[0x24]()`, 640 slots of `0x80` bytes at
  `profile+0x20`, slot `+0x24` = status (`2` = empty), slot `+0x18` = XTEA-encrypted vehicle id
  (key `0xF99F28`, same decryptor as the game's `0x6DDD70`; low dword = dealer id). It then
  walks the loaded Flash contexts (`[0x10E8080]`, next `+0xAB0`, dictionary `+0x60`/`+0x64`),
  finds the dealer movie by its `/:m_OutOfStock` field and recolours the four `/:Current_car`
  name fields: green if owned, original colour otherwise.
* Nothing about buying, stock or reservation changes. The original call bytes are verified and
  the exe build (timestamp `0x468E36BE`) is checked before patching.

## Install

Copy `TDU_DealerOwnedGreen.asi` and `TDU_DealerOwnedGreen.ini` into the game's `scripts` folder
(Ultimate ASI Loader, e.g. `dinput8.dll`).

## Build

* Linux: `./build.sh` (clang + lld-link + llvm-dlltool).
* Windows / MSVC: `build.bat` from an x86 Native Tools prompt.

## Changelog

* v2: fixed "every car is green". v1 looked cars up in `[0x11F2CDC]`, which turned out to be the
  catalogue of all vehicle configurations (every dealer car is in it), not the player's garage.
  The log is now appended instead of recreated at every launch, and each line shows how many
  garage cars were found.

## AI disclosure

This plugin was written with the help of AI. The reverse engineering of
`TestDriveUnlimited.exe` (with Ghidra through the REA tooling) and of the dealer UI in
`shops.bnk`, plus the C source, build scripts and this README, were produced by Claude
(Anthropic, model `claude-opus-5-5`) under the direction of the repository owner, who tested
the result in-game. Addresses and structure offsets come from static analysis of the 1.66A exe
and may not apply to other builds.
