#include "CrashHandler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <format>
#include <span>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first
#include <csignal>
#include <dbghelp.h>
#include <process.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#endif

namespace ad::crash
{

namespace
{

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

// ---------------------------------------------------------------------------
// Preallocated state (the handler must not allocate)
// ---------------------------------------------------------------------------

constexpr size_t kLineSize   = 240;
constexpr size_t kPathSize   = 1024;
constexpr size_t kReasonSize = 512;
constexpr int    kMaxFrames  = 64;

struct Crumb
{
    std::atomic<uint64_t>       seq{0}; // 1 + sequence number of the stored line, 0 -- empty
    std::array<char, kLineSize> text{};
};

std::array<Crumb, kBreadcrumbs> crumbs;
std::atomic<uint64_t>           next_seq{0};
std::atomic<bool>               installed{false};
std::atomic<bool>               in_handler{false};
std::array<char, 128>           app_name{};
std::array<char, kReasonSize>   terminate_reason{};
const auto                      kStartTime = std::chrono::steady_clock::now();

#ifdef _WIN32
using PathChar = wchar_t;
#else
using PathChar = char;
#endif
std::array<PathChar, kPathSize> report_dir{};

void CopyText(std::span<char> dst, std::string_view src)
{
    const size_t n = std::min(src.size(), dst.size() - 1);
    std::memcpy(dst.data(), src.data(), n);
    dst[n] = '\0';
}

// ---------------------------------------------------------------------------
// Report writer: raw file output, no allocations
// ---------------------------------------------------------------------------

class Writer
{
public:
    Writer()                          = default;
    Writer(const Writer &)            = delete;
    Writer &operator=(const Writer &) = delete;
    ~Writer() { Close(); }

    /// <dir>/crash-<unix time>-<pid>.<ext>
    bool Open(uint64_t time, uint64_t pid, const char *ext)
    {
        std::array<PathChar, kPathSize + 64> path{};
        size_t                               n   = 0;
        auto                                 put = [&](PathChar c)
        {
            if (n + 1 < path.size())
            {
                path[n++] = c;
            }
        };
        for (size_t i = 0; i < report_dir.size() && report_dir[i] != 0; ++i)
        {
            put(report_dir[i]);
        }
        for (const char *s = "/crash-"; *s != '\0'; ++s)
        {
            put(static_cast<PathChar>(*s));
        }
        std::array<char, 24> digits{};
        for (const char c : std::string_view(Digits(digits, time, 12)))
        {
            put(static_cast<PathChar>(c));
        }
        put(static_cast<PathChar>('-'));
        for (const char c : std::string_view(Digits(digits, pid, 0)))
        {
            put(static_cast<PathChar>(c));
        }
        put(static_cast<PathChar>('.'));
        for (const char *s = ext; *s != '\0'; ++s)
        {
            put(static_cast<PathChar>(*s));
        }
        path[n] = 0;
#ifdef _WIN32
        _file = CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return _file != INVALID_HANDLE_VALUE;
#else
        _fd = ::open(path.data(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
        return _fd >= 0;
#endif
    }

    void Close()
    {
#ifdef _WIN32
        if (_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(_file);
            _file = INVALID_HANDLE_VALUE;
        }
#else
        if (_fd >= 0)
        {
            ::close(_fd);
            _fd = -1;
        }
#endif
    }

    void Raw(const char *data, size_t size)
    {
#ifdef _WIN32
        DWORD written = 0;
        ::WriteFile(_file, data, static_cast<DWORD>(size), &written, nullptr);
#else
        while (size > 0)
        {
            const ssize_t r = ::write(_fd, data, size);
            if (r <= 0)
            {
                return;
            }
            data += r;
            size -= static_cast<size_t>(r);
        }
#endif
    }

    Writer &operator<<(const char *s)
    {
        Raw(s, std::strlen(s));
        return *this;
    }

    Writer &Num(uint64_t v, int width = 0)
    {
        std::array<char, 24> buf{};
        return *this << Digits(buf, v, width);
    }

    Writer &Hex(uint64_t v)
    {
        std::array<char, 24> buf{};
        size_t               i = buf.size() - 1;
        buf[i]                 = '\0';
        do
        {
            buf[--i] = "0123456789abcdef"[v & 0xfU];
            v >>= 4U;
        } while (v != 0 && i > 2);
        buf[--i] = 'x';
        buf[--i] = '0';
        return *this << &buf[i];
    }

#ifdef _WIN32
    [[nodiscard]] HANDLE Handle() const { return _file; }
#else
    [[nodiscard]] int Fd() const { return _fd; }
#endif

    static const char *Digits(std::array<char, 24> &buf, uint64_t v, int width)
    {
        size_t i = buf.size() - 1;
        buf[i]   = '\0';
        int n    = 0;
        do
        {
            buf[--i] = static_cast<char>('0' + v % 10);
            v /= 10;
            ++n;
        } while ((v != 0 || n < width) && i > 0);
        return &buf[i];
    }

private:
#ifdef _WIN32
    HANDLE _file = INVALID_HANDLE_VALUE;
#else
    int _fd = -1;
#endif
};

uint64_t UnixTime() { return static_cast<uint64_t>(std::time(nullptr)); }

uint64_t ProcessId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

void WriteHeader(Writer &w, const char *reason, uint64_t time)
{
    w << "Animated Diagrams crash report\n";
    w << "Application: " << app_name.data() << "\n";
    w << "Time (unix): ";
    w.Num(time) << "\n";
    w << "Process: ";
    w.Num(ProcessId()) << "\n";
    w << "Reason: " << reason << "\n";
    if (terminate_reason[0] != '\0')
    {
        w << "Termination: " << terminate_reason.data() << "\n";
    }
}

void WriteBreadcrumbs(Writer &w)
{
    w << "\nLast events (oldest first):\n";
    const uint64_t end   = next_seq.load(std::memory_order_acquire);
    const uint64_t begin = end > kBreadcrumbs ? end - kBreadcrumbs : 0;
    for (uint64_t s = begin; s < end; ++s)
    {
        const Crumb &c = crumbs[s % kBreadcrumbs];
        if (c.seq.load(std::memory_order_acquire) == s + 1)
        {
            w << "  " << c.text.data() << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Platform handlers
// ---------------------------------------------------------------------------

#ifdef _WIN32

bool symbols = false;

const char *ExceptionName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_IN_PAGE_ERROR:
        return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case 0xE06D7363:
        return "C++ exception";
    default:
        return "unhandled exception";
    }
}

void WriteStack(Writer &w)
{
    w << "\nStack trace:\n";
    std::array<void *, kMaxFrames>                                   frames{};
    const USHORT                                                     n = CaptureStackBackTrace(0, kMaxFrames, frames.data(), nullptr);
    alignas(SYMBOL_INFO) std::array<char, sizeof(SYMBOL_INFO) + 256> sym_buf{};
    auto                                                            *sym = reinterpret_cast<SYMBOL_INFO *>(sym_buf.data());
    for (USHORT i = 0; i < n; ++i)
    {
        const auto addr = reinterpret_cast<uint64_t>(frames[i]);
        w << "  #";
        w.Num(i) << " ";
        w.Hex(addr);
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module) != 0)
        {
            std::array<char, MAX_PATH> name{};
            GetModuleFileNameA(module, name.data(), static_cast<DWORD>(name.size()));
            const char *base = std::strrchr(name.data(), '\\');
            w << " " << (base != nullptr ? base + 1 : name.data()) << "+";
            w.Hex(addr - reinterpret_cast<uint64_t>(module));
        }
        if (symbols)
        {
            std::memset(sym_buf.data(), 0, sym_buf.size());
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen   = 255;
            DWORD64 disp      = 0;
            if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym) != FALSE)
            {
                w << " " << sym->Name << "+";
                w.Hex(disp);
            }
        }
        w << "\n";
    }
}

void WriteReport(const char *reason, EXCEPTION_POINTERS *ep)
{
    const uint64_t time = UnixTime();
    {
        Writer w;
        if (!w.Open(time, ProcessId(), "txt"))
        {
            return;
        }
        WriteHeader(w, reason, time);
        if (ep != nullptr)
        {
            w << "Address: ";
            w.Hex(reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress)) << "\n";
        }
        WriteStack(w);
        WriteBreadcrumbs(w);
        w << "\nMinidump: crash-";
        w.Num(time, 12) << "-";
        w.Num(ProcessId()) << ".dmp\n";
    }
    Writer dump;
    if (dump.Open(time, ProcessId(), "dmp"))
    {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump.Handle(),
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                          ep != nullptr ? &mei : nullptr, nullptr, nullptr);
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS *ep)
{
    if (!in_handler.exchange(true))
    {
        WriteReport(ExceptionName(ep->ExceptionRecord->ExceptionCode), ep);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void FatalWithoutContext(const char *reason)
{
    if (!in_handler.exchange(true))
    {
        WriteReport(reason, nullptr);
    }
    TerminateProcess(GetCurrentProcess(), 3);
}

void OnAbortSignal(int /*sig*/) { FatalWithoutContext("abort()"); }
void OnPureCall() { FatalWithoutContext("pure virtual function call"); }
void OnInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    FatalWithoutContext("invalid parameter passed to a CRT function");
}

void InstallPlatform()
{
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    symbols = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    SetUnhandledExceptionFilter(OnUnhandledException);
    std::signal(SIGABRT, OnAbortSignal);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
}

#else

constexpr std::array kSignals = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};

const char *SignalName(int sig)
{
    switch (sig)
    {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGABRT:
        return "SIGABRT (abort)";
    case SIGBUS:
        return "SIGBUS (bus error)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
    default:
        return "fatal signal";
    }
}

void OnSignal(int sig, siginfo_t *info, void * /*context*/)
{
    if (!in_handler.exchange(true))
    {
        const uint64_t time = UnixTime();
        Writer         w;
        if (w.Open(time, ProcessId(), "txt"))
        {
            WriteHeader(w, SignalName(sig), time);
            if (sig != SIGABRT && info != nullptr)
            {
                w << "Address: ";
                w.Hex(reinterpret_cast<uintptr_t>(info->si_addr)) << "\n";
            }
            w << "\nStack trace (module(+offset) [address]; resolve with addr2line -e <module> <offset>):\n";
            std::array<void *, kMaxFrames> frames{};
            const int                      n = ::backtrace(frames.data(), kMaxFrames);
            ::backtrace_symbols_fd(frames.data(), n, w.Fd());
            WriteBreadcrumbs(w);
        }
    }
    // default action (core dump / termination) with the original signal
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

std::array<char, 64 * 1024> alt_stack{}; // handler stack: works after a stack overflow too

void InstallPlatform()
{
    {
        // load libgcc's unwinder now: the first backtrace() call may allocate
        std::array<void *, 4> warmup{};
        ::backtrace(warmup.data(), static_cast<int>(warmup.size()));
    }
    stack_t ss{};
    ss.ss_sp    = alt_stack.data();
    ss.ss_size  = alt_stack.size();
    ss.ss_flags = 0;
    ::sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = OnSignal;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (const int sig : kSignals)
    {
        ::sigaction(sig, &sa, nullptr);
    }
}

#endif

void OnTerminate()
{
    const char *what = "std::terminate() without an active exception";
    std::string text;
    if (const std::exception_ptr e = std::current_exception(); e != nullptr)
    {
        try
        {
            std::rethrow_exception(e);
        }
        catch (const std::exception &ex)
        {
            text = std::string("uncaught exception: ") + ex.what();
            what = text.c_str();
        }
        catch (...)
        {
            what = "uncaught exception of an unknown type";
        }
    }
    CopyText(terminate_reason, what);
    std::abort(); // reported by the SIGABRT handler
}

} // namespace

void Install(const std::filesystem::path &dir, std::string_view app)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
    const std::wstring native = dir.wstring();
#else
    const std::string native = dir.string();
#endif
    const size_t n = std::min(native.size(), report_dir.size() - 1);
    std::copy_n(native.data(), n, report_dir.data());
    report_dir[n] = 0;
    CopyText(app_name, app);
    if (installed.exchange(true))
    {
        return; // the directory and the name are updated; handlers are already in place
    }
    if (HandlesFatalErrors())
    {
        InstallPlatform();
        std::set_terminate(OnTerminate);
    }
}

bool Installed() { return installed.load(); }

bool HandlesFatalErrors() { return !kSanitized; }

void Breadcrumb(std::string_view line)
{
    const auto     ms  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStartTime).count();
    const uint64_t seq = next_seq.fetch_add(1, std::memory_order_acq_rel);
    Crumb         &c   = crumbs[seq % kBreadcrumbs];
    c.seq.store(0, std::memory_order_release); // being rewritten
    const auto       head = std::format_to_n(c.text.data(), c.text.size() - 1, "[{:>4}.{:03}s] ", ms / 1000, ms % 1000);
    const size_t     used = static_cast<size_t>(head.out - c.text.data());
    std::string_view rest = line.substr(0, c.text.size() - 1 - used);
    // one report line per breadcrumb
    for (size_t i = 0; i < rest.size(); ++i)
    {
        c.text[used + i] = rest[i] == '\n' || rest[i] == '\r' ? ' ' : rest[i];
    }
    c.text[used + rest.size()] = '\0';
    c.seq.store(seq + 1, std::memory_order_release);
}

std::vector<std::string> Breadcrumbs()
{
    std::vector<std::string> out;
    const uint64_t           end   = next_seq.load(std::memory_order_acquire);
    const uint64_t           begin = end > kBreadcrumbs ? end - kBreadcrumbs : 0;
    for (uint64_t s = begin; s < end; ++s)
    {
        const Crumb &c = crumbs[s % kBreadcrumbs];
        if (c.seq.load(std::memory_order_acquire) == s + 1)
        {
            out.emplace_back(c.text.data());
        }
    }
    return out;
}

std::vector<std::filesystem::path> Reports(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> out;
    std::error_code                    ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (name.starts_with("crash-") && name.ends_with(".txt"))
        {
            out.push_back(it->path());
        }
    }
    // names start with a zero-padded unix time: lexicographic order is chronological
    std::ranges::sort(out,
                      [](const auto &a, const auto &b)
                      {
                          return a.filename() > b.filename();
                      });
    return out;
}

} // namespace ad::crash
