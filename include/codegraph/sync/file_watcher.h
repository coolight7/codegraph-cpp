/**
 * file_watcher.h — 文件监听接口（跨平台）
 *
 * 抽象接口，平台实现隐藏在 cpp 文件中。
 * 通过静态工厂方法 FileWatcher::create() 创建平台对应的实例。
 *
 * 使用模式：
 *   auto watcher = FileWatcher::create("/path/to/project", &running);
 *   watcher->set_callback([](const std::string& path, uint32_t mask) {
 *       // 处理文件变更
 *   });
 *   while (running) {
 *       watcher->poll(1000);  // 阻塞等待事件，超时 1 秒
 *   }
 *
 * 监听的事件：修改、创建、删除、移动
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace codegraph {

/// 跨平台文件变更事件掩码
constexpr uint32_t FILE_EVENT_MODIFIED   = 0x00000001;
constexpr uint32_t FILE_EVENT_CREATED    = 0x00000002;
constexpr uint32_t FILE_EVENT_DELETED    = 0x00000004;
constexpr uint32_t FILE_EVENT_MOVED_FROM = 0x00000008;
constexpr uint32_t FILE_EVENT_MOVED_TO   = 0x00000010;

class FileWatcher {
public:
    /** 事件回调类型：(文件路径, 事件掩码)。 */
    using Callback = std::function<void(const std::string& path, uint32_t mask)>;

    virtual ~FileWatcher() = default;

    /**
     * 静态工厂方法：创建平台对应的文件监听器。
     *
     * @param path 要监听的根目录
     * @param running 外部原子标志位指针（Windows 下用于优雅退出，Linux 下忽略）
     * @return 平台对应的 FileWatcher 实例
     * @throws std::runtime_error 如果初始化失败
     */
    static std::unique_ptr<FileWatcher> create(
        const std::string& path,
        std::atomic<bool>* running = nullptr);

    /** 设置事件回调。 */
    virtual void set_callback(Callback cb) = 0;

    /** 对单个目录添加监听。 */
    virtual void add_watch(const std::string& path) = 0;

    /** 递归监听目录树中的所有子目录。 */
    virtual void add_watch_recursive(const std::string& path) = 0;

    /**
     * 轮询文件变更事件。
     *
     * @param timeout_ms 超时时间（毫秒）
     */
    virtual void poll(int timeout_ms = 1000) = 0;

    /** 停止监听。 */
    virtual void stop() = 0;

protected:
    FileWatcher() = default;
};

}  // namespace codegraph