<p align="center">
  <img src="docs/img/steam-link-icon.png" width="56" height="56" alt="Steam Link">
  &nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/img/steam-frame-logo-dark.svg">
    <img src="docs/img/steam-frame-logo.svg" width="240" alt="Steam Frame">
  </picture>
</p>

<p align="center">
  <img src="docs/img/flag-en.png" width="24" alt="English">&nbsp;<b>English</b>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="README.de.md"><img src="docs/img/flag-de.png" width="24" alt="Deutsch">&nbsp;Deutsch</a>
</p>

> [!IMPORTANT]
> **Supported SteamVR on the Frame: 2.18.2 · build 1791331621**

# Steam Link Native Passthrough

**For Steam Frame.** Bring supported PC VR games into your room over Steam Link.

## Native passthrough — no colour replacement

Unlike Virtual Desktop's colour-key passthrough, this keeps every game colour and
lets the game tell the Frame which parts of the picture are see-through, and by
how much. The headset blends that information with its own live camera view, so
partially transparent objects can blend with your real room too.

**[Install on the Frame](docs/INSTALL.md#directly-on-the-frame)** ·
**[Install via SSH](docs/INSTALL.md#from-a-pc-over-ssh)** ·
[Compatibility](docs/COMPATIBILITY.md)

## Supported games

| Game | Required mod |
|---|---|
| Gloomhaven Digital | [GloomhavenVR](https://github.com/McFredward/GloomhavenVR), with **Steam Link native passthrough** integration |

Other games need their own integration. Installing this patch alone does not
add mixed reality to an unmodified game.

## Requirements

| Requirement | Supported |
|---|---|
| Setup | Steam Frame streaming from a Windows PC through Steam Link |
| SteamVR on the Frame | **2.18.2 — build 1791331621** |

**Experimental:** some edges and motion still need improvement.
See [known limits and update support](docs/COMPATIBILITY.md).

## Get started

1. Install using [Desktop Mode on the Frame](docs/INSTALL.md#directly-on-the-frame)
   or [SSH from your PC](docs/INSTALL.md#from-a-pc-over-ssh).
2. Start your PC VR stream and enable native passthrough in the supported game or mod.
   Follow its mixed-reality setup instructions.

The installation stays on the Frame between sessions. You can turn passthrough
off in the game whenever you want.

[Update](docs/INSTALL.md#update) · [Uninstall](docs/INSTALL.md#uninstall) ·
[Recover if something goes wrong](docs/INSTALL.md#recovery)

## For developers

[Integrate your game](docs/DEVELOPING.md) ·
[Runtime compatibility and limitations](docs/COMPATIBILITY.md)

## Credits

Based on [Jake Lurie's steam-frame-chroma-key](https://github.com/spyjacob/steam-frame-chroma-key).
Its history and [MIT license](LICENSE) are preserved; the native-opacity path is
developed here by McFredward and contributors.

Unofficial project, not affiliated with Valve.
