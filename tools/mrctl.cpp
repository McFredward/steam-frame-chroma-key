// mrctl — tiny OpenVR helper for the mrchroma prototype.
//   mrctl get-bool  <section> <key>
//   mrctl set-bool  <section> <key> <0|1>
//   mrctl remove    <section> <key>
//   mrctl timing    [seconds]      frame-timing statistics from vrcompositor
// Runs as a background OpenVR app: never starts SteamVR, never takes focus.
#include <openvr.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace vr;

static int usage() {
    fprintf(stderr, "usage: mrctl get-bool|set-bool|remove <section> <key> [0|1]\n       mrctl timing [seconds]\n");
    return 2;
}

static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5));
    return v[i];
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    EVRInitError e = VRInitError_None;
    VR_Init(&e, VRApplication_Background);
    if (e != VRInitError_None) {
        fprintf(stderr, "VR_Init failed: %s\n", VR_GetVRInitErrorAsEnglishDescription(e));
        return 1;
    }
    int rc = 0;
    std::string cmd = argv[1];
    if ((cmd == "get-bool" || cmd == "set-bool" || cmd == "remove") && argc >= 4) {
        EVRSettingsError se = VRSettingsError_None;
        if (cmd == "set-bool" && argc >= 5) {
            VRSettings()->SetBool(argv[2], argv[3], atoi(argv[4]) != 0, &se);
            if (se) fprintf(stderr, "SetBool error %d\n", se), rc = 1;
        } else if (cmd == "remove") {
            VRSettings()->RemoveKeyInSection(argv[2], argv[3], &se);
            if (se) fprintf(stderr, "Remove error %d\n", se), rc = 1;
        }
        se = VRSettingsError_None;
        bool v = VRSettings()->GetBool(argv[2], argv[3], &se);
        printf("%s/%s = %s%s\n", argv[2], argv[3], v ? "true" : "false", se ? " (unset/default)" : "");
    } else if (cmd == "timing") {
        double secs = argc >= 3 ? atof(argv[2]) : 10.0;
        // Let the window fill, then pull the full history in one call.
        auto t0 = std::chrono::steady_clock::now();
        std::vector<Compositor_FrameTiming> all;
        uint32_t lastIdx = 0;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
            Compositor_FrameTiming ft[128];
            for (auto &f : ft) f.m_nSize = sizeof(Compositor_FrameTiming);
            uint32_t n = VRCompositor()->GetFrameTimings(ft, 128);
            for (uint32_t i = 0; i < n; i++)
                if (ft[i].m_nFrameIndex > lastIdx) all.push_back(ft[i]);
            if (n) lastIdx = std::max(lastIdx, ft[n - 1].m_nFrameIndex);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        std::sort(all.begin(), all.end(),
                  [](auto &a, auto &b) { return a.m_nFrameIndex < b.m_nFrameIndex; });
        std::vector<double> interval, clientInt, compGpu, appGpu, newFrame, presents, reproj;
        uint32_t dropped = 0, mispresented = 0, reprojected = 0;
        for (size_t i = 0; i < all.size(); i++) {
            auto &f = all[i];
            if (i) interval.push_back((f.m_flSystemTimeInSeconds - all[i - 1].m_flSystemTimeInSeconds) * 1e3 /
                                      std::max<uint32_t>(1, f.m_nFrameIndex - all[i - 1].m_nFrameIndex));
            clientInt.push_back(f.m_flClientFrameIntervalMs);
            compGpu.push_back(f.m_flCompositorRenderGpuMs);
            appGpu.push_back(f.m_flTotalRenderGpuMs);
            newFrame.push_back(f.m_flNewFrameReadyMs);
            dropped += f.m_nNumDroppedFrames;
            mispresented += f.m_nNumMisPresented;
            if (f.m_nReprojectionFlags & (VRCompositor_ReprojectionAsync | VRCompositor_ReprojectionMotion)) reprojected++;
        }
        printf("frames=%zu  (%.1f s)\n", all.size(), secs);
        if (!all.empty()) {
            printf("  display interval ms : p50 %.2f  p95 %.2f   (=> %.1f Hz)\n", pct(interval, .5), pct(interval, .95),
                   1000.0 / std::max(1e-3, pct(interval, .5)));
            printf("  client interval ms  : p50 %.2f  p95 %.2f\n", pct(clientInt, .5), pct(clientInt, .95));
            printf("  client GPU ms       : p50 %.2f  p95 %.2f\n", pct(appGpu, .5), pct(appGpu, .95));
            printf("  compositor GPU ms   : p50 %.2f  p95 %.2f\n", pct(compGpu, .5), pct(compGpu, .95));
            printf("  new-frame-ready ms  : p50 %.2f  p95 %.2f   (frame start -> client frame ready)\n",
                   pct(newFrame, .5), pct(newFrame, .95));
            printf("  dropped=%u  mispresented=%u  reprojected=%u (%.1f%%)\n", dropped, mispresented, reprojected,
                   100.0 * reprojected / all.size());
        }
    } else {
        rc = usage();
    }
    VR_Shutdown();
    return rc;
}
