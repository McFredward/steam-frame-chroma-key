"""mrchroma PC sync — keeps the PC SteamVR home background in step with the headset.

When you stream from the PC with no game running (e.g. the SteamVR desktop
view), the PC draws its own background (aurora/mountains, floor, grid) into the
video, so the headset's chroma key has nothing to key.  This helper watches the
headset over `ssh frame` and, live through SteamVR's settings API:

  headset `mrtoggle.sh bg on`   -> PC background = solid key colour, floor/grid/base stations hidden
  headset `mrtoggle.sh bg off`  -> PC defaults (mountains, floor)
  key colour changed on headset -> PC background colour follows

It idles while SteamVR is not running and acknowledges each apply back to the
headset (~/.local/share/mrchroma/pcbg.applied) so the headset panel can show
whether the PC is in sync.

  pythonw mrchroma_pcsync.pyw              run (normally started from the Startup folder)
  python  mrchroma_pcsync.pyw --install    add to Windows Startup and start it
  python  mrchroma_pcsync.pyw --uninstall  remove from Startup, stop it, restore PC defaults
  python  mrchroma_pcsync.pyw --once       apply the current headset state once and exit (debug)
"""
import ctypes
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vrsettings_win  # noqa: E402

HOST = os.environ.get("MRCHROMA_HOST", "frame")
STATE_DIR = os.path.join(os.environ.get("LOCALAPPDATA", HERE), "mrchroma")
LOG = os.path.join(STATE_DIR, "pcsync.log")
CACHE = os.path.join(STATE_DIR, "pcsync.json")
STARTUP_LNK = os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs\Startup",
                           "mrchroma_pcsync.lnk")
NO_WINDOW = 0x08000000
HIDE_KEYS = ("showFloor", "showGridCircles", "drawTrackingReferences")
WATCH = (r'S=~/.local/share/mrchroma/mrkey.state; B=~/.config/mrchroma/pcbg; p=; '
         r'while :; do s="$(cat $S 2>/dev/null)|$(cat $B 2>/dev/null)"; '
         r'if [ "$s" != "$p" ]; then echo "$s"; p="$s"; fi; sleep 0.3; done')

os.makedirs(STATE_DIR, exist_ok=True)


def log(*a):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + " ".join(str(x) for x in a)
    try:
        if os.path.exists(LOG) and os.path.getsize(LOG) > 512 * 1024:
            os.replace(LOG, LOG + ".old")
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except OSError:
        pass
    if sys.stdout and sys.stdout.isatty():
        print(line)


def vrserver_pid():
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq vrserver.exe", "/NH", "/FO", "CSV"],
                             capture_output=True, text=True, creationflags=NO_WINDOW, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for line in out.splitlines():
        parts = line.strip().strip('"').split('","')
        if parts and parts[0].lower() == "vrserver.exe":
            return parts[1]
    return None


def ssh(cmd, timeout=10):
    return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", HOST, cmd],
                          capture_output=True, text=True, creationflags=NO_WINDOW, timeout=timeout)


def parse(line, last_rgb):
    """'chroma R G B thr soft spill|on' -> ((r,g,b), 'on')"""
    state, _, bg = line.partition("|")
    rgb = last_rgb
    f = state.split()
    if len(f) >= 4 and f[0] == "chroma":
        try:
            rgb = tuple(max(0.0, min(1.0, float(x))) for x in f[1:4])
        except ValueError:
            pass
    return rgb, ("on" if bg.strip() == "on" else "off")


def hexcol(rgb):
    return "#%02X%02X%02X" % tuple(round(c * 255) for c in rgb)


def apply(rgb, bg):
    with vrsettings_win.Session() as s:
        if bg == "on":
            s.set_string("steamvr", "background", hexcol(rgb))
            for k in HIDE_KEYS:
                s.set_bool("steamvr", k, False)
        else:
            for k in ("background",) + HIDE_KEYS:
                try:
                    s.remove("steamvr", k)
                except vrsettings_win.VRSettingsError:
                    pass
    value = "on %s" % hexcol(rgb) if bg == "on" else "off"
    try:
        ssh("echo '%s' > ~/.local/share/mrchroma/pcbg.applied" % value)
    except (OSError, subprocess.SubprocessError):
        pass
    log("applied", value)
    return value


def load_cache():
    try:
        c = json.load(open(CACHE))
        return tuple(c.get("rgb", (0, 0, 1))), c.get("bg", "off")
    except (OSError, ValueError):
        return (0.0, 0.0, 1.0), "off"


def save_cache(rgb, bg):
    try:
        json.dump({"rgb": list(rgb), "bg": bg}, open(CACHE, "w"))
    except OSError:
        pass


def run():
    rgb, bg = load_cache()
    applied_for = None   # (vrserver pid, value) last applied
    while True:
        pid = vrserver_pid()
        if not pid:
            time.sleep(3)
            continue
        log("SteamVR running (pid %s); watching %s" % (pid, HOST))
        try:
            proc = subprocess.Popen(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                                     "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=3", HOST, WATCH],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                    creationflags=NO_WINDOW)
        except OSError as e:
            log("ssh failed:", e)
            time.sleep(10)
            continue
        last_check = time.time()
        try:
            for line in proc.stdout:
                rgb, bg = parse(line.strip(), rgb)
                save_cache(rgb, bg)
                want = (pid, "on %s" % hexcol(rgb) if bg == "on" else "off")
                if want != applied_for:
                    try:
                        applied_for = (pid, apply(rgb, bg))
                    except vrsettings_win.VRSettingsError as e:
                        log("apply failed:", e)
                if time.time() - last_check > 5:
                    last_check = time.time()
                    if vrserver_pid() != pid:
                        break
        finally:
            proc.kill()
        # ssh ended (headset asleep / SteamVR restarted); if SteamVR went away, wait for it
        time.sleep(3)


def single_instance():
    h = ctypes.windll.kernel32.CreateMutexW(None, False, "Local\\mrchroma_pcsync")
    return h and ctypes.windll.kernel32.GetLastError() != 183   # ERROR_ALREADY_EXISTS


def pythonw():
    exe = sys.executable
    w = os.path.join(os.path.dirname(exe), "pythonw.exe")
    return w if os.path.exists(w) else exe


def install():
    ps = ("$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%s');$s.TargetPath='%s';"
          "$s.Arguments='\"%s\"';$s.WorkingDirectory='%s';$s.WindowStyle=7;$s.Save()"
          % (STARTUP_LNK, pythonw(), os.path.abspath(__file__), HERE))
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=True, creationflags=NO_WINDOW)
    subprocess.Popen([pythonw(), os.path.abspath(__file__)], cwd=HERE, creationflags=NO_WINDOW)
    print("installed:", STARTUP_LNK)
    print("running in the background; log:", LOG)


def uninstall():
    if os.path.exists(STARTUP_LNK):
        os.remove(STARTUP_LNK)
    me = os.getpid()
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*mrchroma_pcsync*' "
                    "-and $_.ProcessId -ne %d } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" % me],
                   creationflags=NO_WINDOW)
    if vrserver_pid():
        apply((0, 0, 1), "off")
        print("PC SteamVR background restored to default")
    else:
        print("SteamVR not running: background setting left as is (start SteamVR and run --uninstall again to reset)")
    print("removed", STARTUP_LNK)


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "--install":
        install()
    elif arg == "--uninstall":
        uninstall()
    elif arg == "--once":
        r = ssh("cat ~/.local/share/mrchroma/mrkey.state; echo -n '|'; cat ~/.config/mrchroma/pcbg 2>/dev/null")
        rgb, bg = parse(r.stdout.replace("\n", ""), load_cache()[0])
        print(apply(rgb, bg))
    else:
        if not single_instance():
            sys.exit(0)
        log("pcsync start")
        run()
