#include "DebugLog.h"

#ifdef TWAIN_DEBUG

#include <QCoreApplication>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>

#include <chrono>
#include <stdio.h>
#include <unistd.h>

namespace DebugLog {

namespace {
QFile* g_file = nullptr;
QTextStream* g_stream = nullptr;
QMutex g_mutex;
std::chrono::steady_clock::time_point g_t0;
bool g_initialized = false;
}  // namespace

void init() {
    QMutexLocker lock(&g_mutex);
    if (g_initialized) return;
    g_t0 = std::chrono::steady_clock::now();
    const QString path = QString("/tmp/twain-%1.log").arg(QCoreApplication::applicationPid());
    g_file = new QFile(path);
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        fprintf(stderr, "twain-debug: could not open %s for writing\n",
                qPrintable(path));
        delete g_file;
        g_file = nullptr;
        return;
    }
    g_stream = new QTextStream(g_file);
    g_initialized = true;
    fprintf(stderr, "twain-debug: logging to %s\n", qPrintable(path));
    *g_stream << QString("[t+0.000s] twain-debug started pid=%1\n")
                     .arg(QCoreApplication::applicationPid());
    g_stream->flush();
}

double elapsedSeconds() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - g_t0).count();
}

void logLine(const QString& line) {
    QMutexLocker lock(&g_mutex);
    if (!g_stream) return;
    *g_stream << line << '\n';
    g_stream->flush();
}

ScopedTimer::ScopedTimer(const char* name)
    : m_name(name), m_start(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - m_start).count();
    QString line = QString("%1: %2 ms").arg(QString::fromLatin1(m_name)).arg(ms, 0, 'f', 2);
    if (!m_extra.isEmpty()) line += " (" + m_extra + ")";
    TIFF_LOG(line);
}

}  // namespace DebugLog

#endif  // TWAIN_DEBUG
