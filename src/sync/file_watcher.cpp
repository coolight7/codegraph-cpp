/**
 * file_watcher.cpp — 文件监听实现（跨平台）
 *
 * Linux:   LinuxFileWatcher 使用 inotify API 监听文件系统变更
 * Windows: Win32FileWatcher 使用 ReadDirectoryChangesW API 监听文件系统变更
 *
 * 统一事件掩码（定义在 file_watcher.h）：
 *   FILE_EVENT_MODIFIED, FILE_EVENT_CREATED, FILE_EVENT_DELETED,
 *   FILE_EVENT_MOVED_FROM, FILE_EVENT_MOVED_TO
 */

#include "codegraph/sync/file_watcher.h"

#include <filesystem>
#include <cstring>
#include <stdexcept>

namespace fs = std::filesystem;

#ifdef _WIN32

#include <windows.h>

namespace codegraph {

// ─────────────────────────────────────────────────────────────────────────────
// Win32FileWatcher — 使用 ReadDirectoryChangesW
// ─────────────────────────────────────────────────────────────────────────────

class Win32FileWatcher : public FileWatcher {
public:
    Win32FileWatcher(const std::string& path, std::atomic<bool>* running);
    ~Win32FileWatcher() override;

    void set_callback(Callback cb) override;
    void add_watch(const std::string& path) override;
    void add_watch_recursive(const std::string& path) override;
    void poll(int timeout_ms = 1000) override;
    void stop() override;

private:
    static std::string wide_to_utf8(const wchar_t* wstr);
    static std::wstring utf8_to_wide(const std::string& str);

    Callback callback_;
    std::atomic<bool>* ext_running_ = nullptr;
    bool running_ = false;
    std::unordered_map<std::string, HANDLE> dir_handles_;
    OVERLAPPED overlapped_ = {};
    alignas(DWORD) char buffer_[65536];
};

std::string Win32FileWatcher::wide_to_utf8(const wchar_t* wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring Win32FileWatcher::utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

Win32FileWatcher::Win32FileWatcher(const std::string& path, std::atomic<bool>* running)
    : ext_running_(running) {
    running_ = true;
    memset(&overlapped_, 0, sizeof(overlapped_));
    overlapped_.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped_.hEvent) {
        throw std::runtime_error("Failed to create event for file watcher");
    }
    add_watch_recursive(path);
}

Win32FileWatcher::~Win32FileWatcher() {
    stop();
    for (auto& [path, handle] : dir_handles_) {
        CancelIo(handle);
        CloseHandle(handle);
    }
    if (overlapped_.hEvent) {
        CloseHandle(overlapped_.hEvent);
    }
}

void Win32FileWatcher::set_callback(Callback cb) { callback_ = std::move(cb); }

void Win32FileWatcher::add_watch(const std::string& path) {
    if (dir_handles_.contains(path)) return;

    std::wstring wpath = utf8_to_wide(path);
    HANDLE hDir = CreateFileW(
        wpath.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (hDir == INVALID_HANDLE_VALUE) return;

    dir_handles_[path] = hDir;

    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                   FILE_NOTIFY_CHANGE_DIR_NAME |
                   FILE_NOTIFY_CHANGE_LAST_WRITE |
                   FILE_NOTIFY_CHANGE_SIZE;

    ReadDirectoryChangesW(
        hDir, buffer_, sizeof(buffer_), TRUE, filter, nullptr, &overlapped_, nullptr
    );
}

void Win32FileWatcher::add_watch_recursive(const std::string& path) {
    add_watch(path);
    try {
        for (auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_directory()) {
                add_watch(entry.path().string());
            }
        }
    } catch (...) {
        // Permission errors etc. are non-fatal
    }
}

void Win32FileWatcher::poll(int timeout_ms) {
    if (!running_ || !ext_running_ || !(*ext_running_)) return;

    DWORD wait_result = WaitForSingleObject(overlapped_.hEvent, timeout_ms);

    if (wait_result == WAIT_TIMEOUT) return;
    if (wait_result != WAIT_OBJECT_0) return;

    DWORD bytes_transferred = 0;
    if (!GetOverlappedResult(nullptr, &overlapped_, &bytes_transferred, FALSE)) {
        ResetEvent(overlapped_.hEvent);
        for (auto& [path, handle] : dir_handles_) {
            DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                           FILE_NOTIFY_CHANGE_DIR_NAME |
                           FILE_NOTIFY_CHANGE_LAST_WRITE |
                           FILE_NOTIFY_CHANGE_SIZE;
            ReadDirectoryChangesW(
                handle, buffer_, sizeof(buffer_), TRUE, filter, nullptr, &overlapped_, nullptr
            );
        }
        return;
    }

    if (bytes_transferred == 0) {
        ResetEvent(overlapped_.hEvent);
        return;
    }

    char* ptr = buffer_;
    while (true) {
        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ptr);
        std::wstring wname(info->FileName, info->FileNameLength / sizeof(wchar_t));
        std::string name = wide_to_utf8(wname.c_str());

        for (auto& [dir_path, handle] : dir_handles_) {
            std::string full_path = dir_path + "\\" + name;

            uint32_t mask = 0;
            switch (info->Action) {
                case FILE_ACTION_MODIFIED:        mask = FILE_EVENT_MODIFIED;   break;
                case FILE_ACTION_ADDED:           mask = FILE_EVENT_CREATED;    break;
                case FILE_ACTION_REMOVED:         mask = FILE_EVENT_DELETED;    break;
                case FILE_ACTION_RENAMED_OLD_NAME: mask = FILE_EVENT_MOVED_FROM; break;
                case FILE_ACTION_RENAMED_NEW_NAME: mask = FILE_EVENT_MOVED_TO;   break;
                default: break;
            }

            if (callback_ && mask != 0) {
                callback_(full_path, mask);
            }
            break;
        }

        if (info->NextEntryOffset == 0) break;
        ptr += info->NextEntryOffset;
    }

    ResetEvent(overlapped_.hEvent);

    for (auto& [path, handle] : dir_handles_) {
        DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                       FILE_NOTIFY_CHANGE_DIR_NAME |
                       FILE_NOTIFY_CHANGE_LAST_WRITE |
                       FILE_NOTIFY_CHANGE_SIZE;
        ReadDirectoryChangesW(
            handle, buffer_, sizeof(buffer_), TRUE, filter, nullptr, &overlapped_, nullptr
        );
    }
}

void Win32FileWatcher::stop() { running_ = false; }

// ── Factory ──

std::unique_ptr<FileWatcher> FileWatcher::create(
    const std::string& path,
    std::atomic<bool>* running) {
    return std::make_unique<Win32FileWatcher>(path, running);
}

}  // namespace codegraph

#else  // ────────────────────────────────── Linux ─────────────────────────────

#include <sys/inotify.h>
#include <unistd.h>
#include <sys/select.h>
#include <unordered_map>

namespace codegraph {

// ─────────────────────────────────────────────────────────────────────────────
// LinuxFileWatcher — 使用 inotify
// ─────────────────────────────────────────────────────────────────────────────

class LinuxFileWatcher : public FileWatcher {
public:
    explicit LinuxFileWatcher(const std::string& path);
    ~LinuxFileWatcher() override;

    void set_callback(Callback cb) override;
    void add_watch(const std::string& path) override;
    void add_watch_recursive(const std::string& path) override;
    void poll(int timeout_ms = 1000) override;
    void stop() override;

private:
    Callback callback_;
    bool running_ = false;
    int inotify_fd_ = -1;
    std::unordered_map<int, std::string> watch_map_;
};

LinuxFileWatcher::LinuxFileWatcher(const std::string& path) {
    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        throw std::runtime_error("Failed to initialize inotify");
    }
    running_ = true;
    add_watch_recursive(path);
}

LinuxFileWatcher::~LinuxFileWatcher() {
    stop();
    for (auto& [wd, path] : watch_map_) {
        inotify_rm_watch(inotify_fd_, wd);
    }
    if (inotify_fd_ >= 0) close(inotify_fd_);
}

void LinuxFileWatcher::set_callback(Callback cb) { callback_ = std::move(cb); }

void LinuxFileWatcher::add_watch(const std::string& path) {
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    int wd = inotify_add_watch(inotify_fd_, path.c_str(), mask);
    if (wd >= 0) watch_map_[wd] = path;
}

void LinuxFileWatcher::add_watch_recursive(const std::string& path) {
    add_watch(path);
    try {
        for (auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_directory()) {
                add_watch(entry.path().string());
            }
        }
    } catch (...) {
        // Permission errors etc. are non-fatal
    }
}

void LinuxFileWatcher::poll(int timeout_ms) {
    char buf[4096];
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(inotify_fd_, &fds);

    int ret = select(inotify_fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret < 0) {
        if (errno == EINTR) return;
        return;
    }
    if (ret == 0) return;

    ssize_t len = read(inotify_fd_, buf, sizeof(buf));
    if (len <= 0) return;

    ssize_t i = 0;
    while (i < len) {
        struct inotify_event* event = (struct inotify_event*)&buf[i];
        if (callback_ && event->len > 0) {
            auto it = watch_map_.find(event->wd);
            std::string dir = (it != watch_map_.end()) ? it->second : "";
            std::string filepath = dir.empty() ? std::string(event->name)
                                               : dir + "/" + event->name;

            // Map inotify mask to unified event mask
            uint32_t mask = 0;
            if (event->mask & IN_MODIFY)       mask |= FILE_EVENT_MODIFIED;
            if (event->mask & IN_CREATE)       mask |= FILE_EVENT_CREATED;
            if (event->mask & IN_DELETE)       mask |= FILE_EVENT_DELETED;
            if (event->mask & IN_MOVED_FROM)   mask |= FILE_EVENT_MOVED_FROM;
            if (event->mask & IN_MOVED_TO)     mask |= FILE_EVENT_MOVED_TO;

            callback_(filepath, mask);
        }
        i += sizeof(struct inotify_event) + event->len;
    }
}

void LinuxFileWatcher::stop() { running_ = false; }

// ── Factory ──

std::unique_ptr<FileWatcher> FileWatcher::create(
    const std::string& path,
    std::atomic<bool>* running) {
    (void)running;
    return std::make_unique<LinuxFileWatcher>(path);
}

}  // namespace codegraph

#endif