#pragma once

// Control-file work belongs to the existing background writers, never hooks.
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace frame_compositor_arm {
inline constexpr char Contents[] = "FRAME_COMPOSITOR_RAW/1\n";
enum class Result { Missing, Valid, Invalid };

inline bool path(const char* raw, char* output, std::size_t capacity) noexcept {
    constexpr char suffix[] = "/compositor-raw.jsonl";
    constexpr char name[] = "/capture-arm.marker";
    if (!raw || !output || raw[0] != '/') return false;
    const auto size = strnlen(raw, 1024);
    const auto tail = sizeof(suffix) - 1;
    if (size >= 1024 || size <= tail || std::memcmp(raw + size - tail, suffix, tail)) return false;
    for (std::size_t i = 0; i < size; ++i) {
        const auto c = raw[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '-' || c == '.' || c == '/')) return false;
        if (c == '/' && (i + 1 == size || raw[i + 1] == '/' ||
                (raw[i + 1] == '.' && (raw[i + 2] == '/' || raw[i + 2] == '\0' ||
                (raw[i + 2] == '.' && (raw[i + 3] == '/' || raw[i + 3] == '\0')))))) return false;
    }
    const auto parent = size - tail;
    if (parent > capacity || sizeof(name) > capacity - parent) return false;
    std::memcpy(output, raw, parent);
    std::memcpy(output + parent, name, sizeof(name));
    return true;
}

inline bool stable(const struct stat& a, const struct stat& b) noexcept {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode &&
        a.st_uid == b.st_uid && a.st_gid == b.st_gid && a.st_nlink == b.st_nlink && a.st_size == b.st_size &&
        a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
        a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

inline Result read(const char* name) noexcept {
    const int saved = errno;
    char parent[1024]{};
    if (!name || strnlen(name, sizeof(parent)) >= sizeof(parent)) { errno = saved; return Result::Invalid; }
    std::strcpy(parent, name);
    char* slash = std::strrchr(parent, '/');
    if (!slash || slash == parent || std::strcmp(slash + 1, "capture-arm.marker")) { errno = saved; return Result::Invalid; }
    *slash = 0;
    const int directory = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    struct stat before_dir{}, after_dir{};
    if (directory < 0 || fstat(directory, &before_dir) || !S_ISDIR(before_dir.st_mode) ||
            before_dir.st_uid != geteuid() || (before_dir.st_mode & 07777) != 0700) {
        if (directory >= 0) close(directory);
        errno = saved; return Result::Invalid;
    }
    const int fd = openat(directory, "capture-arm.marker", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        const auto result = errno == ENOENT ? Result::Missing : Result::Invalid;
        const bool same_directory = !lstat(parent, &after_dir) && before_dir.st_dev == after_dir.st_dev &&
            before_dir.st_ino == after_dir.st_ino && before_dir.st_mode == after_dir.st_mode &&
            before_dir.st_uid == after_dir.st_uid;
        close(directory); errno = saved; return same_directory ? result : Result::Invalid;
    }
    struct stat before{}, after{}, current{};
    char bytes[sizeof(Contents)]{};
    const bool owned = !fstat(fd, &before) && S_ISREG(before.st_mode) && before.st_uid == geteuid() &&
        (before.st_mode & 07777) == 0600 && before.st_nlink == 1 && before.st_size == sizeof(Contents) - 1;
    const bool valid = owned && ::read(fd, bytes, sizeof(bytes)) == sizeof(Contents) - 1 &&
        !std::memcmp(bytes, Contents, sizeof(Contents) - 1) && !fstat(fd, &after) &&
        !fstatat(directory, "capture-arm.marker", &current, AT_SYMLINK_NOFOLLOW) &&
        !lstat(parent, &after_dir) && before_dir.st_dev == after_dir.st_dev &&
        before_dir.st_ino == after_dir.st_ino && before_dir.st_mode == after_dir.st_mode &&
        before_dir.st_uid == after_dir.st_uid && stable(before, after) && stable(before, current);
    close(fd); close(directory); errno = saved;
    return valid ? Result::Valid : Result::Invalid;
}
} // namespace frame_compositor_arm
