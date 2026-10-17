#pragma once

// Cold/background-only user-owned diagnostic I/O. Never called by a Vulkan
// recording/submission hook. Directory descriptors pin owned outputs across
// path renames; no configuration or output is followed through a symlink.
#include "../receiver/sha256.h"
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace frame_pixel_io {
constexpr const char* ConfigPath = "/home/steamos/.local/share/GloomhavenVR/SteamLinkMaskProbe/session.conf";
inline std::string defaultConfigPath() {
#ifdef FRAME_PIXEL_RELEASE_RECEIVER
    const char* home=std::getenv("HOME");
    if(!home || home[0]!='/' || strnlen(home,1025)>900)return {};
    std::string path(home);
    if(path.size()>1 && path.back()=='/')path.pop_back();
    return path+"/.local/share/SteamFrameNativePassthrough/session.conf";
#else
    return ConfigPath;
#endif
}
#ifdef FRAME_PIXEL_RELEASE_RECEIVER
inline bool releaseProcessMatches() noexcept {
    // Cold negotiation only: pin the actually executing inode, not a pathname
    // checked before exec. An updated compositor must keep original rendering.
    constexpr off_t bytes=6026096;
    constexpr char expected[]="0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96";
    const int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
    if(fd<0)return false;
    struct stat before{},after{};
    bool valid=!fstat(fd,&before) && S_ISREG(before.st_mode) && before.st_size==bytes;
    frame_receiver::Sha256 hash;std::array<unsigned char,4096> buffer{};
    off_t used=0;
    while(valid && used<bytes){
        const auto want=static_cast<std::size_t>(bytes-used)<buffer.size()?static_cast<std::size_t>(bytes-used):buffer.size();
        const auto count=read(fd,buffer.data(),want);
        if(count<0 && errno==EINTR)continue;
        if(count<=0){valid=false;break;}
        hash.update(buffer.data(),static_cast<std::size_t>(count));used+=count;
    }
    valid=valid && !fstat(fd,&after) && before.st_dev==after.st_dev && before.st_ino==after.st_ino
        && before.st_size==after.st_size && before.st_mtim.tv_sec==after.st_mtim.tv_sec
        && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec;
    close(fd);if(!valid)return false;
    constexpr char digits[]="0123456789abcdef";const auto digest=hash.finish();
    for(std::size_t i=0;i<digest.size();++i)
        if(expected[2*i]!=digits[digest[i]>>4] || expected[2*i+1]!=digits[digest[i]&15])return false;
    return true;
}
#endif
constexpr const char* ConfigMagic = "GHVR_PIXEL_SNAPSHOT/1\n";
constexpr const char* ArmMagic = "GHVR_PIXEL_SNAPSHOT_ARM/1\n";
struct Config {
    std::string captureId, directory;
    int directoryFd = -1;
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    ~Config() { if (directoryFd >= 0) close(directoryFd); }
};
inline bool writeAll(int fd, const void* bytes, std::size_t size) noexcept {
    const auto* cursor = static_cast<const unsigned char*>(bytes);
    while (size) {
        const auto done = ::write(fd, cursor, size);
        if (done < 0 && errno == EINTR) continue;
        if (done <= 0) return false;
        cursor += done; size -= static_cast<std::size_t>(done);
    }
    return true;
}
inline int privateDirectory(const std::string& path) {
    if (path.empty() || path.front() != '/' || path.size() > 1024 || path.back() == '/') return -1;
    int parent = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (std::size_t start = 1; parent >= 0 && start < path.size();) {
        const auto end = path.find('/', start);
        const auto component = path.substr(start, end == std::string::npos ? end : end - start);
        if (component.empty() || component == "." || component == "..") { close(parent); return -1; }
        const int child = openat(parent, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(parent); parent = child;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    struct stat info{};
    if (parent >= 0 && (fstat(parent, &info) || !S_ISDIR(info.st_mode) || info.st_uid != getuid() ||
                       (info.st_mode & 0777) != 0700)) { close(parent); return -1; }
    return parent;
}
inline bool load(const char* path, Config& result) {
    if (!path || path[0] != '/' || result.directoryFd >= 0) return false;
    const std::string full(path);
    const auto splitAt=full.find_last_of('/');
    if(splitAt==std::string::npos || splitAt==0 || splitAt==full.size()-1)return false;
    const int parent=privateDirectory(full.substr(0,splitAt));
    if(parent<0)return false;
    const int fd = openat(parent,full.substr(splitAt+1).c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    close(parent);
    if (fd < 0) return false;
    struct stat info{};
    std::array<char, 2049> bytes{};
    const bool regular = !fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == getuid() &&
        (info.st_mode & 0777) == 0600 && info.st_nlink == 1 && info.st_size > 0 && info.st_size <= 2048;
    const auto count = regular ? read(fd, bytes.data(), bytes.size()) : -1;
    close(fd);
    if (!regular || count != info.st_size || std::memchr(bytes.data(), 0, static_cast<std::size_t>(count))) return false;
    const std::string text(bytes.data(), static_cast<std::size_t>(count));
    if (text.compare(0, std::strlen(ConfigMagic), ConfigMagic)) return false;
    const auto start = std::strlen(ConfigMagic), split = text.find('\n', start);
    if (split != start + 32 || text.back() != '\n' || text.find('\n', split + 1) != text.size() - 1) return false;
    const auto id = text.substr(start, 32);
    bool nonzero = false;
    for (const char ch : id) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
        nonzero = nonzero || ch != '0';
    }
    if (!nonzero) return false;
    const auto folder = text.substr(split + 1, text.size() - split - 2);
    const int directory = privateDirectory(folder);
    if (directory < 0) return false;
    result.captureId = id; result.directory = folder; result.directoryFd = directory;
    return true;
}
inline bool armed(const Config& config) noexcept {
    const int fd = openat(config.directoryFd, "capture.arm", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat info{};
    char bytes[64]{};
    const bool valid = !fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == getuid() &&
        (info.st_mode & 0777) == 0600 && info.st_nlink == 1 && info.st_size == static_cast<off_t>(std::strlen(ArmMagic)) &&
        read(fd, bytes, sizeof(bytes)) == info.st_size && !std::memcmp(bytes, ArmMagic, std::strlen(ArmMagic));
    close(fd); return valid;
}
inline int create(const Config& config, const char* name) noexcept {
    // All callers use fixed filenames, never names from input/native metadata.
    return openat(config.directoryFd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
}
inline std::string digest(const void* data, std::size_t bytes) {
    frame_receiver::Sha256 hash; hash.update(data, bytes);
    const auto value = hash.finish();
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        result[2*i] = digits[value[i] >> 4]; result[2*i+1] = digits[value[i] & 15];
    }
    return result;
}
} // namespace frame_pixel_io
