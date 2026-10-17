# First real-game Steam Link passthrough result, 2026-10-17

The maintainer confirms successful real GloomhavenVR imagery over native Frame
camera passthrough with PC ModBuild686. This supersedes the earlier synthetic-only
visible result; it does not establish complete coverage of every game material,
acceptable motion latency or general compatibility with unmodified games.

Both supplied application logs identify686. Left and right OnRenderImage receipts
report4040×4040 R8G8B8A8_UNorm,8samples and4040×4040 carriers with
`scene-contribution-atlas-v1`. Subsequent preparations again report both eyes.
The visible result was confirmed by the maintainer and three private photographs.
Home-camera images are not published. Source/conformance results cannot replace
that visible confirmation, and the photographs are not pixel/latency measurements.

## Remaining visible defects

The main-menu preview movie is transparent but should be opaque. The mod displays
it on an owned early, depthless opaque backdrop. A reset after opaque rendering
can erase that backdrop's coverage without an accompanying depth sample to stamp
it back. This is a producer-side coverage defect, not a reason to key scene RGB.

The photographs show a broad soft boundary around a nearby panel and narrower
hand edges. The current coverage field is1024² while the RGB field is2048²;
filtering and streaming can soften silhouettes. Existing UI shadows and the
offscreen/canvas alpha chain are separate possible contributors to the broader
panel boundary. No global alpha threshold or silhouette expansion is justified
by these photographs: that would also destroy genuine fractional transparency.

## Current installation is temporary

The deployed r1 library/config/manifest live in the user's home directory. They
do not replace files under `/usr` or `/opt`. There is no automatic expiry or
restart. The loader claims one prepared run with an exclusive `launch.claim`;
a later SteamVR process leaves the original Vulkan route unchanged instead of
reusing that run. Files remaining on disk are not a persistent activation.

The separate public SteamVR `camera/forceSceneAlphaBlending` override can persist
across restarts. Its receipt records the previous effective and stored state;
the coordinated rollback restores that state. A future SteamVR/OpenVR provider
may need renewed attribution before its old pinned restoration tool can run.
Removing the library alone is not a complete restoration of that camera setting.

Original shader and interface checks refuse unknown pipelines rather than
guessing an update's layout. This does not guarantee that an old injected
library can load correctly against every future native runtime. There is no
automatic SteamVR-update migration or receiver-presence handshake yet.

## Application and distribution boundaries

Ordinary Steam Link video does not carry this opacity contract. Another game
needs a producer of the same image/coverage atlas or a compatible adapter;
installing the receiver alone does not make arbitrary games mixed-reality apps.
The PC Steam Link encoder/sender remains unmodified, and camera images stay on
the headset. Applications/other transports without the matching receiver can
display the atlas instead of a decoded game view. The option must not silently
be treated as an inert unsupported setting.

The privately generated replacement module incorporates the original Valve
shader. Do not ship that module or captured Valve shader bodies in a public
archive. A distributable installer must obtain and validate the user's local
original and generate the derivative locally, with compatibility checks and
complete reversible installation. The original chroma-key installer in this
repository does not install the new temporary scene receiver.

## Provenance

Against the preserved upstream main62482ae,24of26baseline files are byte-identical.
Only README and the small compositor-hooks include differ among those baseline
paths. This is a repository-history census, not a percentage of the new runtime.

The new active receiver is built from the new pixel-snapshot loader, cold native
pipeline replacement, source definition/lifetime guards and atlas transformer;
it does not link the original chroma-key layer as its pixel-processing path.
The original public `pc/vrsettings_win.py` helper is retained and used by the
camera-setting adapter. The original compositor route and camera-setting discovery
also supplied the starting point. Credit the upstream project and preserve its
MIT notice, whether this continues as a fork or becomes a separately named
explicit-opacity project. The original keying backend remains available separately.

Private evidence is retained in GloomhavenVR
`.planning/debug/steamlink-real-runtime-20261017/hardware686-success/` and the
receiver's ignored `build/private-native-scene-runtime-20261017-r1/`.
