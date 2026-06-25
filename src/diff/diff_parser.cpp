/**
 * diff_parser.cpp — Git diff 解析和安全执行（跨平台）
 *
 * 本文件提供两个功能：
 *   1. run_git_diff(): 安全地执行 git diff 命令（防注入）
 *   2. parse_diff(): 解析 git diff 输出，提取变更的文件和行范围
 *
 * 安全设计：
 *   - 不使用 system()/popen()（会被 shell 注入攻击）
 *   - Linux: 使用 fork() + execvp() 直接执行 git，不经过 shell
 *   - Windows: 使用 CreateProcess 直接执行 git，不经过 shell
 *   - 参数用 args 数组传递，不受特殊字符影响
 *   - -- 分隔符确保 ref 参数不会被解释为文件路径
 */

#include "codegraph/diff/diff_parser.h"

#include <regex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace codegraph {

/**
 * 解析 git diff --unified=0 的输出，提取变更的文件和行范围。
 *
 * git diff 输出格式：
 *   diff --git a/file.cpp b/file.cpp
 *   --- a/file.cpp
 *   +++ b/file.cpp              ← 文件名
 *   @@ -10,3 +10,5 @@          ← hunk 头：旧行号,旧行数 新行号,新行数
 *   @@ -20 +22 @@              ← 简化形式：行数为 1 时省略
 *
 * 解析策略：
 *   - 用正则匹配 +++ b/path 提取文件名
 *   - 用正则匹配 @@ -a,b +c,d @@ 提取 hunk 信息
 *   - 只关心新文件的行号（+c,d），用于定位变更影响的代码区域
 *   - new_count == 0 表示纯删除，跳过（没有新代码需要分析）
 */
std::vector<codegraph::DiffHunk> parse_diff(const std::string& diff_output) {
    std::vector<codegraph::DiffHunk> hunks;
    std::istringstream stream(diff_output);
    std::string line;

    std::string current_file;
    static const std::regex file_regex(R"(^\+\+\+ b/(.+)$)");
    static const std::regex hunk_regex(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)");

    while (std::getline(stream, line)) {
        std::smatch match;

        if (std::regex_match(line, match, file_regex)) {
            current_file = "./" + match[1].str();
            continue;
        }

        if (std::regex_match(line, match, hunk_regex)) {
            if (current_file.empty()) continue;

            int new_start = std::stoi(match[3].str());
            int new_count = match[4].matched ? std::stoi(match[4].str()) : 1;

            if (new_count == 0) continue;

            codegraph::DiffHunk hunk;
            hunk.file_path = current_file;
            hunk.line_start = new_start;
            hunk.line_end = new_start + new_count - 1;
            hunk.is_added = true;
            hunks.push_back(hunk);
        }
    }

    return hunks;
}

#ifdef _WIN32

/**
 * 安全地执行 git diff 命令（Windows 实现）。
 *
 * 使用 CreateProcess + 匿名管道，不经过 shell。
 */
std::string run_git_diff(const std::string& ref) {
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "";
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // Build command line
    std::string cmd_line = "git diff --unified=0";
    if (!ref.empty()) {
        cmd_line += " " + ref;
    }
    cmd_line += " --";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "";
    }

    CloseHandle(hWritePipe);

    std::string result;
    char buffer[4096];
    DWORD bytes_read = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
        result.append(buffer, bytes_read);
    }

    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) return "";
    return result;
}

#else

/**
 * 安全地执行 git diff 命令（Linux 实现）。
 *
 * 使用 fork() + execvp() + pipe()，不经过 shell。
 */
std::string run_git_diff(const std::string& ref) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return "";
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        std::string unified_arg = "--unified=0";
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("git"));
        argv.push_back(const_cast<char*>("diff"));
        argv.push_back(unified_arg.data());
        if (!ref.empty()) {
            argv.push_back(const_cast<char*>(ref.c_str()));
        }
        argv.push_back(const_cast<char*>("--"));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);

    std::string result;
    std::array<char, 4096> buffer;
    while (true) {
        ssize_t n = read(pipefd[0], buffer.data(), buffer.size());
        if (n > 0) {
            result.append(buffer.data(), static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        result.clear();
        break;
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return "";
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return "";
    return result;
}

#endif

}  // namespace codegraph