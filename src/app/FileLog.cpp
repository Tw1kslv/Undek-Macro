#include "app/FileLog.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace undek {
namespace {
std::mutex g_mutex;
std::string g_dir;
bool g_enabled = true;

// Held open between lines. A macro writes a line per statement, and opening the
// file every time was most of what logging cost.
std::ofstream g_out;
std::string g_out_dir;
std::string g_out_date;
std::uintmax_t g_open_bytes = 0;

// Long sessions write a line per statement, and nothing used to stop the file
// from growing until the disk noticed.
constexpr std::uintmax_t kMaxLogBytes = 4u * 1024u * 1024u;
// Roughly what the timestamp prefix adds to each line.
constexpr std::uintmax_t kLineOverhead = 32u;
} // namespace

void FileLog::init(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (g_out.is_open()) g_out.close();
    g_out_dir.clear();
    g_out_date.clear();
}

void FileLog::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_enabled = enabled;
    if (!enabled && g_out.is_open()) g_out.close();
}

void FileLog::write(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled || g_dir.empty()) return;

    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream date;
    date << std::put_time(&tm, "%Y%m%d");
    const std::string path = g_dir + "/undek-" + date.str() + ".log";

    // Reopen on a new day (or after init moved the folder) so the file name
    // still says which day it belongs to.
    if (!g_out.is_open() || g_out_dir != g_dir || g_out_date != date.str()) {
        if (g_out.is_open()) g_out.close();
        g_out.open(path, std::ios::app);
        g_out_dir = g_dir;
        g_out_date = date.str();
        g_open_bytes = 0;
        std::error_code size_ec;
        const auto existing = std::filesystem::file_size(path, size_ec);
        if (!size_ec) g_open_bytes = static_cast<std::uintmax_t>(existing);
        if (!g_out) return;
    }

    if (g_open_bytes + line.size() + kLineOverhead > kMaxLogBytes) {
        // Keep one older file: enough to read the run that just went wrong
        // without the log taking over the disk.
        g_out.close();
        std::error_code ec;
        std::filesystem::remove(path + ".1", ec);
        std::filesystem::rename(path, path + ".1", ec);
        g_out.open(path, std::ios::trunc);
        g_open_bytes = 0;
        if (!g_out) return;
    }

    g_out << std::put_time(&tm, "%H:%M:%S") << "  " << line << "\n";
    // Flushed per line on purpose: the log is what a failed run is diagnosed
    // from, and an open stream is already far cheaper than reopening it.
    g_out.flush();
    g_open_bytes += line.size() + kLineOverhead;
}

} // namespace undek
