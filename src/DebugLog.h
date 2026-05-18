#pragma once

#ifdef TWAIN_DEBUG

#include <QString>
#include <chrono>

namespace DebugLog {

void init();
void logLine(const QString& line);
double elapsedSeconds();

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name);
    ~ScopedTimer();
    void note(const QString& extra) { m_extra = extra; }

private:
    const char* m_name;
    std::chrono::steady_clock::time_point m_start;
    QString m_extra;
};

}  // namespace DebugLog

#define TIFF_PASTE_INNER(a, b) a##b
#define TIFF_PASTE(a, b) TIFF_PASTE_INNER(a, b)

#define TIFF_LOG(msg) \
    ::DebugLog::logLine(QString("[t+%1s] ") \
        .arg(::DebugLog::elapsedSeconds(), 0, 'f', 3) + (msg))
#define TIFF_SCOPED(name) \
    ::DebugLog::ScopedTimer TIFF_PASTE(_tiffScoped_, __LINE__)(name)
#define TIFF_SCOPED_VAR(var, name) ::DebugLog::ScopedTimer var(name)
#define TIFF_SCOPED_NOTE(var, extra) (var).note(extra)

#else

namespace DebugLog {
inline void init() {}
}  // namespace DebugLog

#define TIFF_LOG(msg) ((void)0)
#define TIFF_SCOPED(name) ((void)0)
#define TIFF_SCOPED_VAR(var, name) ((void)0)
#define TIFF_SCOPED_NOTE(var, extra) ((void)0)

#endif
