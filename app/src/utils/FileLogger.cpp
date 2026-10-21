#include "FileLogger.hpp"

#include "Settings.hpp"
#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace FileLog {
namespace {

// ---------------------------------------------------------------------------
// Why this is no longer a plain std::fprintf(file, ...) sink.
//
// brls::Logger::log() formats a message and then writes it to the configured
// FILE *, all while holding borealis' global log mutex. When that FILE is the
// log on the SD card, every single log line becomes a synchronous FAT/sdmmc
// write with the whole application queued behind the mutex. One slow write
// (FAT metadata sync, block erase, ...) therefore freezes every thread that
// logs - the decode thread included - for the better part of a second.
//
// That is exactly what the on-device log recorded:
//
//     Input queue reached maximum size limit       x168 in 0.48s (~350/s)
//     STUTTER decode queue: overflowed
//     StreamingView: drew 1076ms after the previous frame, restarting ...
//
// The renderer only presented 6.58 frames that second while the host kept
// delivering a healthy 61.8 fps, so nothing about the network was wrong: the
// client simply stopped running.
//
// So the logging threads no longer touch the filesystem at all. They copy each
// finished line into a bounded in-memory ring; a single writer thread owns the
// FILE and is the only thread that ever talks to the SD card.
// ---------------------------------------------------------------------------

// A line longer than this gets truncated. Keeps the ring's memory footprint
// predictable when a subsystem decides to log something enormous.
constexpr size_t kMaxLineLength = 360;

// ~0.75 MB worst case. Past this we drop and say so rather than grow without
// bound or block the producer.
constexpr size_t kRingCapacity = 2048;

// Flush cadence. Small enough that a crash or a HOME-menu teardown loses
// almost nothing, large enough that the ring is almost always drained in one
// go.
//
// This was 200 ms, which turned a 5 lines/s telemetry rate into five separate
// FAT transactions per second. Each one is a blocking write to the SD card and
// a metadata sync, and the cost lands on the whole system rather than on this
// thread alone - which is why crackling audio shows up more readily with the SD
// log switched on. 500 ms costs at most half a second of log on a crash and
// cuts those transactions by 60%.
constexpr auto kWriterInterval = std::chrono::milliseconds(500);

// Repeated identical messages are collapsed into a single line per window.
// Without this one misbehaving subsystem turns the log into a 350 line/s SD
// write storm - which is what used to happen.
constexpr auto kRepeatWindow = std::chrono::milliseconds(1000);

struct Entry {
    std::string text; // the line exactly as borealis formatted it
    std::string key;  // same line minus the timestamp, for de-duplication
};

std::mutex g_mutex;
std::deque<Entry> g_ring;
std::FILE* g_log_file = nullptr;
std::FILE* g_sink = nullptr;
std::thread g_writer;
std::atomic<bool> g_stop { false };
bool g_running = false;
uint64_t g_droppedLines = 0;

// Completed into whole lines by writeCallback().
std::string g_staging;

// Repeated-message collapsing state.
bool g_hasPending = false;
Entry g_pending;
uint64_t g_pendingRepeats = 0;
std::chrono::steady_clock::time_point g_pendingWindowStart {};

// borealis writes "<HH:MM:SS.mmm>\033<color>[LEVEL]\033[0m <message>". The
// timestamp makes every line unique, so the de-duplication key drops it.
std::string dedupeKey(const std::string& line) {
    if (line.size() > 12 && line[2] == ':' && line[5] == ':' && line[8] == '.')
        return line.substr(12);

    return line;
}

// g_mutex must be held.
void pushLocked(Entry&& entry) {
    if (g_ring.size() >= kRingCapacity) {
        g_droppedLines++;
        return;
    }

    g_ring.emplace_back(std::move(entry));
}

// g_mutex must be held. Also called once per writer interval so that a message
// which keeps repeating still shows up in the log instead of sitting pending
// forever.
void flushPendingLocked() {
    if (!g_hasPending)
        return;

    if (g_pendingRepeats > 1)
        g_pending.text += " [repeated x" + std::to_string(g_pendingRepeats) + "]";

    pushLocked(std::move(g_pending));
    g_pending = Entry {};
    g_pendingRepeats = 0;
    g_hasPending = false;
}

// g_mutex must be held.
void ingestLocked(const std::string& line) {
    const std::string key = dedupeKey(line);
    const auto now = std::chrono::steady_clock::now();

    if (g_hasPending && key == g_pending.key &&
        now - g_pendingWindowStart < kRepeatWindow) {
        g_pendingRepeats++;
        return;
    }

    flushPendingLocked();

    g_pending.text = line;
    g_pending.key = key;
    g_pendingRepeats = 1;
    g_pendingWindowStart = now;
    g_hasPending = true;
}

// The write end of the sink handed to brls::Logger. newlib may deliver partial
// lines, so whole lines are reassembled here before they reach the ring. This
// runs while borealis holds its log mutex, so it must stay cheap: no I/O, no
// allocations beyond the queue push.
int writeCallback(void* /*cookie*/, const char* buffer, size_t length) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_running)
        return static_cast<int>(length);

    g_staging.append(buffer, length);

    size_t consumed = 0;
    while (true) {
        const size_t newline = g_staging.find('\n', consumed);
        if (newline == std::string::npos)
            break;

        size_t end = newline;
        if (end > consumed && g_staging[end - 1] == '\r')
            end--;

        std::string line = g_staging.substr(consumed, end - consumed);
        consumed = newline + 1;

        if (line.size() > kMaxLineLength)
            line.resize(kMaxLineLength);

        if (!line.empty())
            ingestLocked(line);
    }

    if (consumed > 0)
        g_staging.erase(0, consumed);

    // A producer that never emits a newline must not grow this buffer forever.
    if (g_staging.size() > kMaxLineLength * 4)
        g_staging.clear();

    return static_cast<int>(length);
}

void writerLoop() {
#ifdef __SWITCH__
    // Deliberately the lowest priority a normal process can ask for. This
    // thread only ever waits on the SD card; at the default priority it can
    // still be scheduled ahead of the audio and decode threads on a core they
    // share, and a blocked FAT write there is a dropped audio frame. Logging is
    // allowed to lose lines, it is not allowed to cost a frame.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3F);
#endif

    std::string batch;

    while (true) {
        std::FILE* file = nullptr;
        bool ringEmpty = false;

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            file = g_log_file;

            flushPendingLocked();

            while (!g_ring.empty()) {
                batch += g_ring.front().text;
                batch += '\n';
                g_ring.pop_front();
            }

            if (g_droppedLines > 0) {
                batch += "[WARNING] FileLog: log ring buffer overflowed, ";
                batch += std::to_string(g_droppedLines);
                batch += " line(s) dropped\n";
                g_droppedLines = 0;
            }

            ringEmpty = g_ring.empty();
        }

        // Deliberately outside the mutex. A slow SD card may only ever delay
        // the next log line here; it can never delay a streaming thread.
        if (!batch.empty() && file != nullptr) {
            std::fwrite(batch.data(), 1, batch.size(), file);
            std::fflush(file);
        }
        batch.clear();

        if (g_stop.load() && ringEmpty)
            break;

        std::this_thread::sleep_for(kWriterInterval);
    }
}

} // namespace

void setEnabled(bool enabled) {
    if (enabled) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_running)
                return;
        }

        const std::string path = Settings::instance().log_path();
        std::FILE* file = std::fopen(path.c_str(), "w");
        if (file == nullptr) {
            brls::Logger::error("FileLog: could not open {} for writing", path);
            return;
        }

        // A write-only stream whose callbacks stay in RAM. This is what keeps
        // borealis' global log mutex cheap to hold.
        std::FILE* sink = fwopen(nullptr, writeCallback);
        if (sink == nullptr) {
            brls::Logger::error("FileLog: could not create the log sink");
            std::fclose(file);
            return;
        }

        // Hand writeCallback() one complete line at a time rather than a 1 KB
        // chunk that would only appear once the buffer happened to fill up.
        std::setvbuf(sink, nullptr, _IOLBF, 0);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_log_file = file;
            g_staging.clear();
            g_ring.clear();
            g_droppedLines = 0;
            g_hasPending = false;
            g_pendingRepeats = 0;
            g_pending = Entry {};
            g_running = true;
        }

        g_stop.store(false);
        g_sink = sink;
        g_writer = std::thread(writerLoop);

        brls::Logger::setLogOutput(sink);
        brls::Logger::info("FileLog: writing to {}", path);
    } else {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_running)
                return;
        }

        brls::Logger::info("FileLog: disabled, back to stdout");

        // newlib defines stdout as a macro, so it cannot be qualified.
        brls::Logger::setLogOutput(stdout);

        // Let anything still sitting in the sink's buffer reach the ring while
        // the sink is still live, then give the FILE slot back - newlib only
        // has a handful of them, so leaking one per toggle would run out.
        if (g_sink != nullptr) {
            std::fflush(g_sink);
            std::fclose(g_sink);
            g_sink = nullptr;
        }

        g_stop.store(true);
        if (g_writer.joinable())
            g_writer.join();

        std::lock_guard<std::mutex> lock(g_mutex);
        g_running = false;

        // Whatever the sink staged but never terminated with a newline.
        if (!g_staging.empty()) {
            std::string tail = g_staging;
            g_staging.clear();

            if (g_log_file != nullptr)
                std::fwrite(tail.data(), 1, tail.size(), g_log_file);
        }

        if (g_log_file != nullptr) {
            std::fflush(g_log_file);
            std::fclose(g_log_file);
            g_log_file = nullptr;
        }
    }
}

bool isEnabled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_running;
}

} // namespace FileLog
