# NeoSymbian legacy

Old, unmodified Symbian S60 apps (3rd/5th edition `.sis` packages) running inside
[neoui](https://github.com/NeoSymbian/neoui), NeoSymbian's Rust touch UI.

neoui owns the display and input, so the stock 2010 window server runs as a *guest*: it draws into a shared chunk,
and `LEGACY.EXE` shows that chunk in its neoui surface and turns neoui's touches and keys into window-server events.
The 2010 application framework, Uikon and Avkon run unchanged on top of it, and so do the apps.

| Path | What |
|---|---|
| `host/` | `LEGACY.EXE`, the neoui app that runs the guest window server and the old apps |
| `classic/` | `CLASSIC.EXE` 0xE0002008, Classic apps: the neoui kit app that lists the old apps (protocol 1.14 `INFO_APPS`) and opens one (`ACTION_OPEN_ENTRY`); its own Rust workspace, GPL-3.0 (links Slint, `classic/LICENSE`) |
| `components.py` | what the legacy environment adds to [build](https://github.com/NeoSymbian/build)'s component registry (AppArc, Uikon, Avkon, their data, the libraries old apps load) |
| `tools/sis.py` | unpacks `.sis` packages into the build, and checks every ROM image's imports (`romcheck`) |
| `tools/mif.py`, `tools/skin_gen.py` | Avkon's icon file and the default skin, with our own artwork where the release has none |
| `sis.lock` | the test packages, by URL and SHA-256 (they are not ours, so they are never committed) |

Fixes to Symbian code itself live in the NeoSymbian fork that owns the file (graphics, classicui, appsupport,
uiresources, cellularsrv, ...), on its `main`; the C++ run time of RVCT-built binaries is in
[kernel](https://github.com/NeoSymbian/kernel) `kernel/eka/compsupp/gcce`.

## Building

`build` pins this repo in its `sources.lock` (fetched as `src/legacy`) and merges `components.py` into its registry;
its build steps run the tools from `src/legacy/tools`. With `SIS=xplore,frozenbubble ./build.sh` the named packages
are installed. The design and milestones are in [docs `legacy.md`](https://github.com/NeoSymbian/docs).

`classic/` is a Rust EXE: `cd classic && cargo fmt --all --check && cargo arm && cargo clippy-arm -- -D warnings`,
then copy `classic/exes.txt`'s line and `target/armv8a-neosymbian-eabi/release/libclassic.a` into build's `rust/`.
Its pins (e32, Slint, neoui) must match neoui's `symbian/Cargo.toml`.

Licence: Apache-2.0 (`LICENSE`), except `classic/`: GPL-3.0 (`classic/LICENSE`), because it links Slint.
