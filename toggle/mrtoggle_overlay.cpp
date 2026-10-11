// mrtoggle_overlay — SteamVR dashboard overlay: chroma-key on/off, key colour,
// similarity / edge softness / spill.  Public OpenVR overlay API only.
//
// Drives the same scripts as the PC panel:
//   mrtoggle.sh toggle|status                  on/off (keeps the generated shader)
//   mrkey.sh chroma R G B thr soft spill       regenerate + hot-reload (turns keying on)
// and polls mrtoggle.sh status / mrkey.state so PC-side changes show up here.
// The UI is software-rendered (FreeType text) and uploaded with SetOverlayRaw.
#include <openvr.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

using namespace vr;
using Clock = std::chrono::steady_clock;

static const int W = 1024, H = 830;

// ---------------------------------------------------------------- settings
struct Key {
    float r = 0, g = 1, b = 0;
    float thr = 0.32f, soft = 0.12f, spill = 0.5f;
};
struct Range { const char *label; float Key::*field; float lo, hi; };
static const Range kSliders[] = {
    {"Similarity", &Key::thr, 0.02f, 0.70f},
    {"Edge softness", &Key::soft, 0.005f, 0.30f},
    {"Spill removal", &Key::spill, 0.0f, 1.0f},
};
struct Preset { const char *name; float r, g, b; };
static const Preset kPresets[] = {
    {"Green", 0, 1, 0}, {"Blue", 0, 0, 1}, {"Magenta", 1, 0, 1}, {"Red", 1, 0, 0}, {"Cyan", 0, 1, 1},
};

static std::string g_home, g_toggle, g_mrkey, g_state;

static std::string runLine(const std::string &cmd) {
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[256] = {};
    if (!fgets(buf, sizeof buf, p)) buf[0] = 0;
    pclose(p);
    std::string s(buf);
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

static bool readState(Key &k) {
    FILE *f = fopen(g_state.c_str(), "r");
    if (!f) return false;
    char mode[32];
    Key n;
    int got = fscanf(f, "%31s %f %f %f %f %f %f", mode, &n.r, &n.g, &n.b, &n.thr, &n.soft, &n.spill);
    fclose(f);
    if (got == 7 && !strcmp(mode, "chroma")) { k = n; return true; }
    return false;
}

static time_t mtimeOf(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? st.st_mtime : 0;
}

// ---------------------------------------------------------------- drawing
struct Canvas {
    std::vector<uint8_t> px = std::vector<uint8_t>(W * H * 4);
    FT_Library ft = nullptr;
    FT_Face bold = nullptr, reg = nullptr;

    void blend(int x, int y, uint32_t rgb, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
        uint8_t *p = &px[(y * W + x) * 4];
        float c[3] = {float(rgb >> 16 & 255), float(rgb >> 8 & 255), float(rgb & 255)};
        float da = p[3] / 255.f, oa = a + da * (1 - a);
        for (int i = 0; i < 3; i++) p[i] = uint8_t((c[i] * a + p[i] * da * (1 - a)) / std::max(oa, 1e-4f));
        p[3] = uint8_t(oa * 255);
    }
    void clear() { std::fill(px.begin(), px.end(), 0); }
    // filled rounded rect with 1px AA edge
    void rrect(float x0, float y0, float x1, float y1, float rad, uint32_t rgb, float a = 1) {
        for (int y = int(y0); y < int(ceil(y1)); y++)
            for (int x = int(x0); x < int(ceil(x1)); x++) {
                float cx = std::clamp(x + .5f, x0 + rad, x1 - rad), cy = std::clamp(y + .5f, y0 + rad, y1 - rad);
                float d = hypotf(x + .5f - cx, y + .5f - cy) - rad;
                blend(x, y, rgb, a * std::clamp(0.5f - d, 0.f, 1.f));
            }
    }
    void ring(float x0, float y0, float x1, float y1, float rad, float w, uint32_t rgb) {
        for (int y = int(y0 - 1); y < int(y1 + 1); y++)
            for (int x = int(x0 - 1); x < int(x1 + 1); x++) {
                float cx = std::clamp(x + .5f, x0 + rad, x1 - rad), cy = std::clamp(y + .5f, y0 + rad, y1 - rad);
                float d = fabsf(hypotf(x + .5f - cx, y + .5f - cy) - rad) - w / 2;
                blend(x, y, rgb, std::clamp(0.5f - d, 0.f, 1.f));
            }
    }
    void circle(float cx, float cy, float r, uint32_t rgb) {
        for (int y = int(cy - r - 1); y <= int(cy + r + 1); y++)
            for (int x = int(cx - r - 1); x <= int(cx + r + 1); x++)
                blend(x, y, rgb, std::clamp(r + 0.5f - hypotf(x + .5f - cx, y + .5f - cy), 0.f, 1.f));
    }
    // returns advance width; align: 0 left, 1 centre, 2 right
    int text(const std::string &s, int x, int y, int size, uint32_t rgb, bool b = false, int align = 0) {
        FT_Face f = b ? bold : reg;
        if (!f) return 0;
        FT_Set_Pixel_Sizes(f, 0, size);
        int w = 0;
        for (char c : s) { if (!FT_Load_Char(f, c, FT_LOAD_DEFAULT)) w += f->glyph->advance.x >> 6; }
        int pen = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
        for (char c : s) {
            if (FT_Load_Char(f, c, FT_LOAD_RENDER)) continue;
            FT_Bitmap &bm = f->glyph->bitmap;
            for (unsigned yy = 0; yy < bm.rows; yy++)
                for (unsigned xx = 0; xx < bm.width; xx++)
                    blend(pen + f->glyph->bitmap_left + xx, y - f->glyph->bitmap_top + yy, rgb,
                          bm.buffer[yy * bm.pitch + xx] / 255.f);
            pen += f->glyph->advance.x >> 6;
        }
        return w;
    }
};

static uint32_t rgbOf(float r, float g, float b) {
    auto c = [](float v) { return uint32_t(std::clamp(v, 0.f, 1.f) * 255 + 0.5f); };
    return c(r) << 16 | c(g) << 8 | c(b);
}
static void hsv(float h, float &r, float &g, float &b) {
    h = fmodf(h, 1.f) * 6;
    float x = 1 - fabsf(fmodf(h, 2.f) - 1);
    int i = int(h);
    float t[6][3] = {{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}};
    r = t[i % 6][0]; g = t[i % 6][1]; b = t[i % 6][2];
}
static float hueOf(float r, float g, float b) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    if (d < 1e-4f) return 0;
    float h = mx == r ? fmodf((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h /= 6;
    return h < 0 ? h + 1 : h;
}

// ---------------------------------------------------------------- layout
struct Rect { float x0, y0, x1, y1; bool in(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; } };
static const Rect kToggle = {664, 28, 996, 128};
static Rect presetRect(int i) { return {300.f + i * 140, 168, 300.f + i * 140 + 120, 248}; }
static const Rect kHue = {300, 290, 996, 350};
static Rect sliderRect(int i) { return {300, 418.f + i * 100, 996, 466.f + i * 100}; }
static const Rect kBgOn = {300, 706, 644, 772}, kBgOff = {652, 706, 996, 772};

// PC home background: wanted state lives on the headset, the PC helper acks what it applied
struct PcBg { bool on = false; std::string applied; };
enum Sync { SyncOk, SyncWaiting, SyncStale };

enum Drag { DragNone = -1, DragHue = 10 };   // 0..2 = slider index

static void render(Canvas &cv, const Key &k, const std::string &status, bool busy, int drag, const PcBg &bg, Sync sync) {
    cv.clear();
    cv.rrect(0, 0, W, H, 40, 0x1c1d22, 0.94f);
    cv.ring(4, 4, W - 4, H - 4, 36, 4, 0x4a4d57);
    cv.text("CHROMA KEY", 40, 98, 56, 0xe8e8e8, true);

    bool on = status == "on";
    uint32_t tc = busy ? 0x9a9a9a : on ? 0x5ae678 : 0xeb5f55;
    cv.rrect(kToggle.x0, kToggle.y0, kToggle.x1, kToggle.y1, 50, busy ? 0x34343a : on ? 0x173a22 : 0x3d1d1d);
    cv.ring(kToggle.x0, kToggle.y0, kToggle.x1, kToggle.y1, 50, 6, tc);
    cv.text(busy ? "..." : on ? "ON" : "OFF", (kToggle.x0 + kToggle.x1) / 2, 103, 64, tc, true, 1);

    // key colour
    cv.text("Key colour", 40, 200, 34, 0xc8c8c8);
    cv.rrect(40, 214, 250, 272, 18, rgbOf(k.r, k.g, k.b));
    cv.ring(40, 214, 250, 272, 18, 3, 0xe8e8e8);
    for (int i = 0; i < 5; i++) {
        Rect r = presetRect(i);
        const Preset &p = kPresets[i];
        bool sel = fabsf(p.r - k.r) + fabsf(p.g - k.g) + fabsf(p.b - k.b) < 0.02f;
        cv.rrect(r.x0, r.y0, r.x1, r.y1, 16, rgbOf(p.r, p.g, p.b));
        cv.ring(r.x0, r.y0, r.x1, r.y1, 16, sel ? 6 : 2, sel ? 0xffffff : 0x6a6d77);
        cv.text(p.name, (r.x0 + r.x1) / 2, r.y1 + 30, 24, 0xb0b0b0, false, 1);
    }
    for (int x = int(kHue.x0); x < int(kHue.x1); x++) {
        float r, g, b;
        hsv((x - kHue.x0) / (kHue.x1 - kHue.x0), r, g, b);
        for (int y = int(kHue.y0) + 10; y < int(kHue.y1) - 10; y++) cv.blend(x, y, rgbOf(r, g, b), 1);
    }
    float hx = kHue.x0 + hueOf(k.r, k.g, k.b) * (kHue.x1 - kHue.x0);
    cv.ring(hx - 10, kHue.y0, hx + 10, kHue.y1, 8, drag == DragHue ? 6 : 4, 0xffffff);
    cv.text("Hue", 40, 335, 30, 0xb0b0b0);

    for (int i = 0; i < 3; i++) {
        const Range &s = kSliders[i];
        Rect r = sliderRect(i);
        float v = k.*(s.field), t = (v - s.lo) / (s.hi - s.lo);
        float cy = (r.y0 + r.y1) / 2;
        cv.text(s.label, 40, int(cy + 12), 34, 0xc8c8c8);
        cv.rrect(r.x0, cy - 7, r.x1, cy + 7, 7, 0x3a3c44);
        cv.rrect(r.x0, cy - 7, r.x0 + t * (r.x1 - r.x0), cy + 7, 7, 0x5a9cff);
        cv.circle(r.x0 + t * (r.x1 - r.x0), cy, drag == i ? 24 : 20, 0xf0f0f0);
        char buf[16];
        snprintf(buf, sizeof buf, "%.2f", v);
        cv.text(buf, int(r.x1), int(r.y0 - 6), 26, 0x9a9a9a, false, 2);
    }
    // desktop (PC home) background
    cv.text("Desktop", 40, 734, 34, 0xc8c8c8);
    cv.text("background", 40, 770, 34, 0xc8c8c8);
    for (int i = 0; i < 2; i++) {
        const Rect &r = i == 0 ? kBgOn : kBgOff;
        bool sel = (i == 0) == bg.on;
        cv.rrect(r.x0, r.y0, r.x1, r.y1, 18, sel ? 0x2b4f8a : 0x2a2c33);
        cv.ring(r.x0, r.y0, r.x1, r.y1, 18, sel ? 4 : 2, sel ? 0x8fb8ff : 0x4a4d57);
        if (i == 0) cv.rrect(r.x0 + 18, r.y0 + 20, r.x0 + 44, r.y1 - 20, 6, rgbOf(k.r, k.g, k.b));
        cv.text(i == 0 ? "Key colour" : "Mountains", (r.x0 + r.x1) / 2 + (i == 0 ? 14 : 0), int(r.y1 - 21), 30,
                sel ? 0xffffff : 0xa0a0a0, sel, 1);
    }
    const char *st = sync == SyncOk ? "PC in sync" : sync == SyncWaiting ? "waiting for PC..." : "PC helper not responding";
    cv.text(st, 996, 700, 22, sync == SyncOk ? 0x5ae678 : sync == SyncWaiting ? 0xe0b050 : 0xeb5f55, false, 2);
    cv.text(on ? "Changes apply when you release the trigger"
               : "Changing colour or sliders turns keying back on", W / 2, H - 22, 24, 0x80838c, false, 1);
}

// ---------------------------------------------------------------- main
int main(int argc, char **argv) {
    const char *home = getenv("HOME");
    g_home = home ? home : "/home/steamos";
    g_toggle = argc > 1 && argv[1][0] != '-' ? argv[1] : g_home + "/mr-chroma/mrtoggle.sh";
    std::string dir = g_toggle.substr(0, g_toggle.rfind('/'));
    g_mrkey = dir + "/mrkey.sh";
    g_state = g_home + "/.local/share/mrchroma/mrkey.state";
    std::string self = argv[0];
    char real[4096];
    if (realpath("/proc/self/exe", real)) self = real;
    std::string resDir = self.substr(0, self.rfind('/'));

    Canvas cv;
    FT_Init_FreeType(&cv.ft);
    FT_New_Face(cv.ft, "/usr/share/fonts/noto/NotoSans-Bold.ttf", 0, &cv.bold);
    FT_New_Face(cv.ft, "/usr/share/fonts/noto/NotoSans-Regular.ttf", 0, &cv.reg);
    if (!cv.reg) cv.reg = cv.bold;

    if (argc > 2 && !strcmp(argv[1], "--render")) {   // layout check without SteamVR: --render out.ppm
        Key k;
        readState(k);
        render(cv, k, "on", false, DragNone, PcBg{true, ""}, SyncOk);
        FILE *f = fopen(argv[2], "wb");
        fprintf(f, "P6 %d %d 255\n", W, H);
        for (int i = 0; i < W * H; i++) {
            float a = cv.px[i * 4 + 3] / 255.f;   // composite over mid grey
            for (int c = 0; c < 3; c++) fputc(int(cv.px[i * 4 + c] * a + 96 * (1 - a)), f);
        }
        fclose(f);
        return 0;
    }

    EVRInitError ie;
    VR_Init(&ie, VRApplication_Overlay);
    if (ie != VRInitError_None) {
        fprintf(stderr, "VR_Init: %s\n", VR_GetVRInitErrorAsEnglishDescription(ie));
        return 1;
    }
    IVROverlay *ov = VROverlay();
    VROverlayHandle_t main = k_ulOverlayHandleInvalid, thumb = k_ulOverlayHandleInvalid;
    EVROverlayError oe = ov->CreateDashboardOverlay("mrchroma.toggle", "Chroma Key", &main, &thumb);
    if (oe != VROverlayError_None) {
        fprintf(stderr, "CreateDashboardOverlay: %s\n", ov->GetOverlayErrorNameFromEnum(oe));
        VR_Shutdown();
        return 1;
    }
    ov->SetOverlayWidthInMeters(main, 2.0f);
    ov->SetOverlayInputMethod(main, VROverlayInputMethod_Mouse);
    HmdVector2_t scale = {{float(W), float(H)}};
    ov->SetOverlayMouseScale(main, &scale);
    ov->SetOverlayFromFile(thumb, (resDir + "/icon.png").c_str());

    Key key;
    readState(key);
    time_t stateMtime = mtimeOf(g_state);
    std::string status = runLine(g_toggle + " status");
    const std::string bgWantPath = g_home + "/.config/mrchroma/pcbg";
    const std::string bgAckPath = g_home + "/.local/share/mrchroma/pcbg.applied";
    auto readWord = [](const std::string &p) {
        std::string s;
        if (FILE *f = fopen(p.c_str(), "r")) {
            char b[64] = {};
            if (fgets(b, sizeof b, f)) s = b;
            fclose(f);
        }
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        return s;
    };
    PcBg bg;
    bg.on = readWord(bgWantPath) == "on";
    bg.applied = readWord(bgAckPath);
    auto mismatchSince = Clock::now();
    // does the PC's ack match what the headset wants? (colour compared within 1/255 per channel)
    auto inSync = [&]() {
        if (!bg.on) return bg.applied == "off";
        unsigned r, g, b;
        if (sscanf(bg.applied.c_str(), "on #%2x%2x%2x", &r, &g, &b) != 3) return false;
        auto near = [](unsigned v, float f) { return abs(int(v) - int(lroundf(f * 255))) <= 1; };
        return near(r, key.r) && near(g, key.g) && near(b, key.b);
    };

    // background worker: runs one command at a time, latest request wins
    std::mutex m;
    std::string pending;
    std::atomic<bool> busy{false};
    bool quit = false;
    std::thread worker([&] {
        while (true) {
            std::string cmd;
            {
                std::lock_guard<std::mutex> l(m);
                if (quit && pending.empty()) return;
                cmd.swap(pending);
            }
            if (cmd.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            busy = true;
            if (system((cmd + " >/dev/null 2>&1").c_str()) != 0) fprintf(stderr, "failed: %s\n", cmd.c_str());
            busy = false;
        }
    });
    auto submit = [&](const std::string &cmd) {
        std::lock_guard<std::mutex> l(m);
        pending = cmd;
        busy = true;
    };
    auto applyKey = [&] {
        char buf[256];
        snprintf(buf, sizeof buf, "%s chroma %.4f %.4f %.4f %.4f %.4f %.4f", g_mrkey.c_str(), key.r, key.g, key.b,
                 key.thr, key.soft, key.spill);
        submit(buf);
    };

    int drag = DragNone;
    bool dirty = true, wasBusy = false;
    auto lastPoll = Clock::now(), lastDraw = Clock::time_point();
    auto setFromX = [&](int which, float x) {
        if (which == DragHue) {
            float h = std::clamp((x - kHue.x0) / (kHue.x1 - kHue.x0), 0.f, 0.999f);
            hsv(h, key.r, key.g, key.b);
        } else {
            const Range &s = kSliders[which];
            Rect r = sliderRect(which);
            float t = std::clamp((x - r.x0) / (r.x1 - r.x0), 0.f, 1.f);
            key.*(s.field) = s.lo + t * (s.hi - s.lo);
        }
        dirty = true;
    };

    while (true) {
        VREvent_t ev;
        while (ov->PollNextOverlayEvent(main, &ev, sizeof ev)) {
            float x = ev.data.mouse.x, y = H - ev.data.mouse.y;   // OpenVR mouse origin is bottom-left
            switch (ev.eventType) {
            case VREvent_MouseButtonDown:
                if (kHue.in(x, y)) { drag = DragHue; setFromX(DragHue, x); }
                for (int i = 0; i < 3; i++) {
                    Rect r = sliderRect(i);
                    Rect hit = {r.x0 - 30, r.y0 - 20, r.x1 + 30, r.y1 + 20};
                    if (hit.in(x, y)) { drag = i; setFromX(i, x); }
                }
                break;
            case VREvent_MouseMove:
                if (drag != DragNone) setFromX(drag, x);
                break;
            case VREvent_MouseButtonUp:
                if (drag != DragNone) {
                    setFromX(drag, x);
                    drag = DragNone;
                    applyKey();
                    status = "on";
                } else if (kToggle.in(x, y)) {
                    submit(g_toggle + " toggle");
                } else if (kBgOn.in(x, y) || kBgOff.in(x, y)) {
                    bg.on = kBgOn.in(x, y);
                    submit(g_toggle + (bg.on ? " bg on" : " bg off"));
                    mismatchSince = Clock::now();
                } else {
                    for (int i = 0; i < 5; i++)
                        if (presetRect(i).in(x, y)) {
                            key.r = kPresets[i].r; key.g = kPresets[i].g; key.b = kPresets[i].b;
                            applyKey();
                            status = "on";
                        }
                }
                dirty = true;
                break;
            }
        }
        while (VRSystem()->PollNextEvent(&ev, sizeof ev))
            if (ev.eventType == VREvent_Quit) {
                VRSystem()->AcknowledgeQuit_Exiting();
                std::lock_guard<std::mutex> l(m);
                quit = true;
            }
        {
            std::lock_guard<std::mutex> l(m);
            if (quit) break;
        }

        auto now = Clock::now();
        if (wasBusy != busy) { wasBusy = busy; dirty = true; }
        if (!busy && drag == DragNone && now - lastPoll > std::chrono::milliseconds(700)) {
            lastPoll = now;
            std::string st = runLine(g_toggle + " status");
            if (st != status) { status = st; dirty = true; }
            time_t mt = mtimeOf(g_state);
            if (mt != stateMtime) { stateMtime = mt; if (readState(key)) dirty = true; }
            bool want = readWord(bgWantPath) == "on";
            std::string ack = readWord(bgAckPath);
            if (want != bg.on || ack != bg.applied) { bg.on = want; bg.applied = ack; dirty = true; }
        }
        static bool lastSynced = true;
        bool synced = inSync();
        if (synced) mismatchSince = now;
        Sync sync = synced ? SyncOk : now - mismatchSince > std::chrono::seconds(6) ? SyncStale : SyncWaiting;
        static Sync lastSync = SyncOk;
        if (sync != lastSync || synced != lastSynced) { lastSync = sync; lastSynced = synced; dirty = true; }
        if (dirty && now - lastDraw > std::chrono::milliseconds(50)) {
            render(cv, key, status, busy, drag, bg, sync);
            ov->SetOverlayRaw(main, cv.px.data(), W, H, 4);
            lastDraw = now;
            dirty = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    VR_Shutdown();
    return 0;
}
