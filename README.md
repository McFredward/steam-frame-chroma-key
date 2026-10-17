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

## Supported now

| Requirement | Supported |
|---|---|
| Game/mod | [GloomhavenVR](https://github.com/McFredward/GloomhavenVR), with **Steam Link native passthrough** integration |
| Setup | Steam Frame streaming from a Windows PC through Steam Link |
| SteamVR on the Frame | **2.18.2 — build 1791331621** |

Other games need their own integration. Installing this patch alone does not
add mixed reality to an unmodified game.

**Experimental:** real GloomhavenVR gameplay has been confirmed; some edges and
motion still need improvement. See [known limits and update support](docs/COMPATIBILITY.md).

## Get started

1. Install using [Desktop Mode on the Frame](docs/INSTALL.md#directly-on-the-frame)
   or [SSH from your PC](docs/INSTALL.md#from-a-pc-over-ssh).
2. In GloomhavenVR, open **VR Options → World → Scenery** and choose **Mixed Reality**.
3. Enable **Steam Link native passthrough** directly below it. Changes apply immediately.

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
