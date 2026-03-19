![Ship of Harkinian](docs/shiptitle.darkmode.png#gh-dark-mode-only)
![Ship of Harkinian](docs/shiptitle.lightmode.png#gh-light-mode-only)

# Ship of Harkinian — Web Edition

Play Ocarina of Time in your browser. This is an Emscripten/WebAssembly fork of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) that runs entirely client-side.

**Play now: [zalo.github.io/Shipwright/](https://zalo.github.io/Shipwright/)**

> This is an independent fork. The upstream project has no plans to merge web support.

## What's Different

This fork adds:

- **Emscripten/WASM build** — the full game compiled to WebAssembly with WebGL rendering
- **Browser ROM loading** — upload `.o2r` files or load them from URLs via hash parameters
- **URL-based configuration** — pass game archives, multiplayer room, player name, and color all via URL
- **PartyKit multiplayer** — WebSocket-based Anchor networking for browser-to-browser multiplayer
- **Mobile support** — touch gamepad with analog stick and all N64 buttons, scaled UI for small screens, iOS audio compatibility, and persistent save files via IndexedDB
- **IndexedDB persistence** — game archives cached on first load, save files persisted automatically
- **Cutscene skipping** — all TimeSaver skip enhancements enabled by default

## Quick Start

You need two archive files: `soh.o2r` (game engine assets) and `oot.o2r` (extracted ROM data). These are not included — you must provide a legally acquired ROM.

### Option 1: Upload files manually
Visit [zalo.github.io/Shipwright/](https://zalo.github.io/Shipwright/) and drag/drop your `soh.o2r` and `oot.o2r` files.

### Option 2: Load via URL parameters
```
https://zalo.github.io/Shipwright/#soh=<SOH_URL>&oot=<OOT_URL>
```

### Option 3: Local file server
```bash
./serve_o2r.sh PlayerName
```
This starts a CORS-enabled file server with a Cloudflare tunnel and prints a ready-to-use URL.

## URL Parameters

All parameters are passed via the URL hash (`#key=value&key2=value2`):

| Parameter | Description | Default |
|-----------|-------------|---------|
| `soh` | URL to `soh.o2r` archive | — |
| `oot` | URL to `oot.o2r` archive | — |
| `room` | Multiplayer room name | `default` |
| `name` | Player display name | — |
| `color` | Player color (hex RGB) | `64FF64` |
| `team` | Team ID for item/flag sync | `default` |

When `room` or `name` are provided, Anchor networking is automatically enabled and the game boots to file select.

## Multiplayer

This fork uses [PartyKit](https://partykit.io/) for WebSocket-based multiplayer via the Anchor networking system. Players in the same room can see each other, trade items, and sync game flags.

The PartyKit server is at `soh/soh/web/partykit/`. Deploy with:
```bash
cd soh/soh/web/partykit && npx partykit deploy
```

## Building

### Prerequisites
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (3.1.64+)
- CMake 3.20+
- Ninja

### Build
```bash
emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-web --parallel $(nproc)
```

### Serve locally
```bash
python3 serve_web.py
# Open http://localhost:8080/soh.html
```

## Keyboard Controls

| N64 | A | B | Z | Start | Analog stick | C buttons | D-Pad |
| - | - | - | - | - | - | - | - |
| Keyboard | X | C | Z | Space | WASD | Arrow keys | TFGH |

| Keys | Action |
| - | - |
| ESC | Toggle menu |
| F11 | Fullscreen |
| Ctrl+R | Reset |

## Credits

Based on [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by HarbourMasters.

- [Official SoH Website](https://www.shipofharkinian.com/)
- [Official SoH Discord](https://discord.com/invite/shipofharkinian)
- [Credits](docs/CREDITS.md)

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>
