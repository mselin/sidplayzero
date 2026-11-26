// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/real_files.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <memory>

namespace spz::platform {

namespace {

FileStatus from_errno(int error) {
    switch (error) {
        case ENOENT:
        case ENOTDIR: return FileStatus::NotFound;
        case EACCES:
        case EPERM:
        case EROFS: return FileStatus::NoAccess;
        case ENOSPC:
        case EDQUOT: return FileStatus::NoSpace;
        case EFBIG: return FileStatus::TooLarge;
        default: return FileStatus::Failed;
    }
}

// A file descriptor that is closed when it goes.
class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) ::close(fd_);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    bool ok() const { return fd_ >= 0; }
    // Closes now, for a caller that wants to know whether the close succeeded.
    int close() {
        const int result = ::close(fd_);
        fd_ = -1;
        return result;
    }

private:
    int fd_;
};

std::string last_part(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') trimmed.pop_back();
    const std::size_t slash = trimmed.rfind('/');
    return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
}

FileInfo describe(const std::string& name, const struct stat& st) {
    FileInfo info;
    info.name = name;
    info.is_link = S_ISLNK(st.st_mode);
    info.is_folder = S_ISDIR(st.st_mode);
    info.is_special = !info.is_link && !info.is_folder && !S_ISREG(st.st_mode);
    if (S_ISREG(st.st_mode)) info.size = static_cast<std::uint64_t>(st.st_size);
    info.modified_s = static_cast<std::int64_t>(st.st_mtim.tv_sec);
    info.modified_ns = static_cast<std::int32_t>(st.st_mtim.tv_nsec);
    return info;
}

FileStatus write_all(int fd, const std::vector<std::uint8_t>& bytes) {
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return from_errno(errno);
        }
        done += static_cast<std::size_t>(n);
    }
    return FileStatus::Ok;
}

FileStatus write_with(const std::string& path, int flags, const std::vector<std::uint8_t>& bytes) {
    Fd fd(::open(path.c_str(), flags | O_WRONLY | O_CREAT | O_CLOEXEC, 0644));
    if (!fd.ok()) return from_errno(errno);
    const FileStatus status = write_all(fd.get(), bytes);
    if (status != FileStatus::Ok) return status;
    if (fd.close() != 0) return from_errno(errno);
    return FileStatus::Ok;
}

// The path itself if something is there, else its nearest existing parent.
std::string nearest_existing(const std::string& path) {
    std::string p = path.empty() ? "." : path;
    struct stat st;
    while (::stat(p.c_str(), &st) != 0) {
        const std::size_t slash = p.rfind('/');
        if (slash == std::string::npos) return ".";
        if (slash == 0) return "/";
        p.resize(slash);
    }
    return p;
}

}  // namespace

FileStatus RealFiles::read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) {
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.ok()) return from_errno(errno);
    struct stat st;
    if (::fstat(fd.get(), &st) != 0) return from_errno(errno);
    if (!S_ISREG(st.st_mode)) return FileStatus::Failed;
    if (static_cast<std::uint64_t>(st.st_size) > limit) return FileStatus::TooLarge;
    std::vector<std::uint8_t> data(static_cast<std::size_t>(st.st_size));
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::read(fd.get(), data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return from_errno(errno);
        }
        if (n == 0) break;   // it became shorter meanwhile
        done += static_cast<std::size_t>(n);
    }
    data.resize(done);
    *out = std::move(data);
    return FileStatus::Ok;
}

FileStatus RealFiles::write(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    return write_with(path, O_TRUNC, bytes);
}

FileStatus RealFiles::append(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    return write_with(path, O_APPEND, bytes);
}

FileStatus RealFiles::map(const std::string& path, std::size_t limit, MappedFile* out) {
    *out = MappedFile();
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.ok()) return from_errno(errno);
    struct stat st;
    if (::fstat(fd.get(), &st) != 0) return from_errno(errno);
    if (!S_ISREG(st.st_mode)) return FileStatus::Failed;
    if (static_cast<std::uint64_t>(st.st_size) > limit) return FileStatus::TooLarge;
    const std::size_t size = static_cast<std::size_t>(st.st_size);
    if (size == 0) {
        *out = MappedFile(nullptr, 0, std::make_shared<int>(0));
        return FileStatus::Ok;
    }
    void* data = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    if (data == MAP_FAILED) return from_errno(errno);
    std::shared_ptr<const void> owner(data, [size](const void* p) { ::munmap(const_cast<void*>(p), size); });
    *out = MappedFile(static_cast<const std::uint8_t*>(data), size, std::move(owner));
    return FileStatus::Ok;
}

FileStatus RealFiles::sync(const std::string& path) {
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.ok()) return from_errno(errno);
    if (::fsync(fd.get()) != 0) return from_errno(errno);
    return FileStatus::Ok;
}

FileStatus RealFiles::rename(const std::string& from, const std::string& to) {
    return ::rename(from.c_str(), to.c_str()) == 0 ? FileStatus::Ok : from_errno(errno);
}

FileStatus RealFiles::remove(const std::string& path) {
    if (::unlink(path.c_str()) == 0) return FileStatus::Ok;
    if (errno == ENOENT) return FileStatus::Ok;
    if (errno == EISDIR || errno == EPERM) {
        if (::rmdir(path.c_str()) == 0 || errno == ENOENT) return FileStatus::Ok;
    }
    return from_errno(errno);
}

FileStatus RealFiles::make_folders(const std::string& path) {
    if (path.empty()) return FileStatus::Failed;
    std::size_t at = 0;
    for (;;) {
        const std::size_t slash = path.find('/', at + 1);
        const std::string part = path.substr(0, slash);
        if (!part.empty() && part != "/") {
            if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return from_errno(errno);
        }
        if (slash == std::string::npos) break;
        at = slash;
    }
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return from_errno(errno);
    return S_ISDIR(st.st_mode) ? FileStatus::Ok : FileStatus::Failed;
}

FileStatus RealFiles::list(const std::string& folder, std::vector<FileInfo>* out) {
    DIR* dir = ::opendir(folder.c_str());
    if (dir == nullptr) return from_errno(errno);
    std::unique_ptr<DIR, int (*)(DIR*)> closer(dir, ::closedir);
    std::vector<FileInfo> entries;
    for (;;) {
        errno = 0;
        const dirent* entry = ::readdir(dir);
        if (entry == nullptr) {
            if (errno != 0) return from_errno(errno);
            break;
        }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat st;
        if (::fstatat(::dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;   // gone meanwhile
        entries.push_back(describe(name, st));
    }
    *out = std::move(entries);
    return FileStatus::Ok;
}

FileStatus RealFiles::info(const std::string& path, FileInfo* out) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return from_errno(errno);
    *out = describe(last_part(path), st);
    return FileStatus::Ok;
}

FileStatus RealFiles::free_bytes(const std::string& path, std::uint64_t* out) {
    struct statvfs vfs;
    if (::statvfs(nearest_existing(path).c_str(), &vfs) != 0) return from_errno(errno);
    *out = static_cast<std::uint64_t>(vfs.f_bavail) * static_cast<std::uint64_t>(vfs.f_frsize);
    return FileStatus::Ok;
}

FileStatus RealFiles::block_size(const std::string& path, std::uint64_t* out) {
    struct statvfs vfs;
    if (::statvfs(nearest_existing(path).c_str(), &vfs) != 0) return from_errno(errno);
    *out = vfs.f_frsize != 0 ? static_cast<std::uint64_t>(vfs.f_frsize) : static_cast<std::uint64_t>(vfs.f_bsize);
    return FileStatus::Ok;
}

bool RealFiles::can_write(const std::string& folder) {
    struct stat st;
    if (::stat(folder.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    return ::access(folder.c_str(), W_OK | X_OK) == 0;
}

}  // namespace spz::platform
