# Steam Frame Native Passthrough

Play a supported PC VR game over **Steam Link** with the Steam Frame's native
passthrough camera behind it. The game supplies its actual opacity separately
from its colours, so partial transparency works without removing a key colour.
The camera image stays on the headset. No modification to Steam's PC streaming
sender is required.

**Experimental.** Real GloomhavenVR gameplay has been visibly confirmed on the
Frame. The new persistent installer has software checks; that earlier headset
test used a temporary receiver, not this persistent installation. Some window
edges and motion still need improvement. See [compatibility and limits](docs/COMPATIBILITY.md).

## Supported games

| Game | Required integration | Status |
|---|---|---|
| Gloomhaven Digital | A [GloomhavenVR](https://github.com/McFredward/GloomhavenVR) build with **Steam Link native passthrough** | Real-game PC VR confirmed with ModBuild686; Windows/D3D11, MultiPass, SDR and Gamma colour space. |

Unmodified Gloomhaven and other games do not supply this opacity format. Installing
this receiver alone does not add mixed reality to them. Glass-like transparency
is supported when the game supplies correct opacity; arbitrary glass, refraction
and every game shader are not guaranteed.

## Install

You need a Steam Frame, a Windows PC with SteamVR and Steam Link, and the supported
game/mod above. Installation runs **on the Frame**, either directly in Desktop
Mode or from a PC over SSH. It builds and checks the receiver locally; no manual
shader download or checksum entry is needed.

The local build needs Bash, Python3, Git for cloning, CMake, `g++` and
`spirv-val`, plus Vulkan headers. These were present on the checked Frame;
the installer reports any missing requirement. Do not unlock the system partition
or replace SteamVR files.

An older experimental/chroma-key installation must first be removed with its
own rollback procedure. The installer refuses those collisions. The first
successful install requests activation; repeating it preserves a state you
explicitly disabled.

### Directly on the Frame

1. Save your game and stop the PC VR stream. Open a terminal in the Frame's
   Desktop Mode.
2. Get the project and install it:

   ```bash
   git clone https://github.com/McFredward/steam-frame-native-passthrough.git
   cd steam-frame-native-passthrough
   bash setup.sh install
   bash setup.sh status
   ```

   Alternatively, use GitHub's **Code → Download ZIP**, extract it, open a terminal
   in the extracted folder and run the last two commands. `chmod` is not needed.
3. Read the result, then **manually restart SteamVR once** to load the installed
   receiver. This interrupts the Frame's VR interface and any stream:

   ```bash
   systemctl --user restart steamvr.service
   ```

### From a PC over SSH

Enable Developer Mode and SSH on the Frame, set its user password, and find its
current network address using the Frame's settings. Valve's
[developer setup](https://partner.steamgames.com/doc/steamhardware/steamframe/setup)
describes device access; menu locations can change with SteamOS updates.

Open PowerShell or a terminal on the PC:

```text
ssh steamos@<frame-ip>
```

The following commands now run **on the Frame**:

```bash
git clone https://github.com/McFredward/steam-frame-native-passthrough.git
cd steam-frame-native-passthrough
bash setup.sh install
bash setup.sh status
```

After saving any work and stopping the stream, manually restart SteamVR once:

```bash
systemctl --user restart steamvr.service
```

Keep the downloaded folder for updates and recovery. Both installation methods
use the same files in your home directory and leave the system partition alone.

## Use with GloomhavenVR

1. Confirm that `bash setup.sh status` admits the installed receiver, then start
   the PC VR stream and GloomhavenVR normally.
2. In VR Options, enable **Mixed Reality**, then **Steam Link native passthrough**.
3. Close VR Options to see the game in your room. Opening VR Options shows the
   ordinary image so you can change settings; there is no automatic timeout.

The game switch controls the current picture. Turning it off restores the
ordinary source when the next game image reaches the headset; it does not remove
the installed receiver. If the receiver is disabled or incompatible, switch this
game option off too: a packed game image without its matching receiver is not
ordinary output.

## Disable, uninstall or recover

Run these from the project folder on the Frame, directly or through SSH:

| Command | Purpose |
|---|---|
| `bash setup.sh status` | Show installation, compatibility and recovery state. |
| `bash setup.sh disable` | Prevent activation on the next SteamVR start. |
| `bash setup.sh enable` | Re-enable a checked installation after resolving a refusal. |
| `bash setup.sh uninstall` | Remove owned files once SteamVR is stopped; report any pending cleanup. |
| `bash setup.sh emergency-off` | Disable activation even when SteamVR's interface or camera API is unavailable. |

Disabling or uninstalling cannot undo pipelines already loaded in a running
SteamVR process. **Finish with a manual SteamVR restart**, after saving work.
The commands never schedule a restart or an expiry timer. Status reports any
camera-setting restoration that could not yet complete.

For a complete uninstall, stop SteamVR manually, run `uninstall`, then start
SteamVR normally. If you ran it while SteamVR was active and it reports pending
cleanup, stop SteamVR and run `uninstall` again before starting it.

If SteamVR does not start normally, use Desktop Mode or SSH, run
`bash setup.sh emergency-off`, then restart SteamVR manually. Keep SSH available
while trying an experimental build. Startup checks and the crash guard reduce
repeat failures; they cannot guarantee that an internal SteamVR change will
never affect the interface.

The installation remains across updates. Each start checks the Frame's runtime
files again; an unknown fingerprint pauses activation. A PC-side SteamVR or Steam
Link update can change the stream without changing those files, so it cannot
always be detected in advance. If the picture becomes packed or incorrect, turn
off **Steam Link native passthrough** in the game, restart the game if needed,
and check `status` and the [compatibility notes](docs/COMPATIBILITY.md). Wait for
a compatibility update instead of bypassing the checks.

To update an existing Git checkout, stop SteamVR manually, run
`git pull --ff-only` and `bash setup.sh install` in the project folder, inspect
`status`, then start SteamVR normally. A loaded library is never overwritten in place.

## More information

- [Compatibility, known limitations and updates](docs/COMPATIBILITY.md)
- [Architecture, game integration and contributing](docs/DEVELOPING.md)
- [Archived chroma-key installation](docs/LEGACY-CHROMA-KEY.md) — a separate older method

## Credits and license

Based on [spyjacob / Jake Lurie's steam-frame-chroma-key](https://github.com/spyjacob/steam-frame-chroma-key),
which established the original chroma-key implementation, native camera-setting
helper and compositor investigation. Its history and [MIT license](LICENSE) are
retained. The explicit-opacity protocol, game integration, native receiver and
persistent installation work are developed in this fork by McFredward and
contributors.

This is an unofficial project, not affiliated with or endorsed by Valve.
