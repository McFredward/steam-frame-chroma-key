// mrtoggle_reg — register/unregister the toggle's app manifest with SteamVR.
//   mrtoggle_reg add <manifest> <app_key>      persistent manifest + autolaunch
//   mrtoggle_reg remove <manifest> <app_key>
#include <openvr.h>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: mrtoggle_reg add|remove manifest app_key\n"); return 2; }
    vr::EVRInitError ie;
    vr::VR_Init(&ie, vr::VRApplication_Utility);
    if (ie != vr::VRInitError_None) { fprintf(stderr, "VR_Init: %d\n", ie); return 1; }
    vr::IVRApplications *apps = vr::VRApplications();
    vr::EVRApplicationError e;
    if (!strcmp(argv[1], "add")) {
        e = apps->AddApplicationManifest(argv[2], false);
        printf("AddApplicationManifest: %s\n", apps->GetApplicationsErrorNameFromEnum(e));
        e = apps->SetApplicationAutoLaunch(argv[3], true);
        printf("SetApplicationAutoLaunch: %s\n", apps->GetApplicationsErrorNameFromEnum(e));
    } else {
        apps->SetApplicationAutoLaunch(argv[3], false);
        e = apps->RemoveApplicationManifest(argv[2]);
        printf("RemoveApplicationManifest: %s\n", apps->GetApplicationsErrorNameFromEnum(e));
    }
    vr::VR_Shutdown();
    return 0;
}
