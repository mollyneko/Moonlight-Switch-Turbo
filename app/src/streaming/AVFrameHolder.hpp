#pragma once

#include "Singleton.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include "Settings.hpp"
#include <mutex>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
}

class AVFrameQueue {
public:
    explicit AVFrameQueue();
    ~AVFrameQueue();

    bool push(AVFrame* item);
    bool pushTransferred(AVFrame* item);
    AVFrame* pop(bool* consumed = nullptr);
    AVFrame* acquireWriteFrame();
    void recycleWriteFrame(AVFrame*& frame);
    void configure(size_t queueLimit, int streamFps,
                   bool transferOwnershipEnabled);
    static size_t capacityFor(size_t configuredQueueSize);

    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t targetDepth() const;
    [[nodiscard]] size_t capacity() const;
    [[nodiscard]] size_t getFakeFrameUsage() const;
    [[nodiscard]] size_t getFramesDropStat() const;
    [[nodiscard]] size_t getEmptyQueueStat() const;
    [[nodiscard]] size_t getRebufferHoldStat() const;
    [[nodiscard]] size_t getOverflowDropStat() const;
    [[nodiscard]] size_t getPacingSkipStat() const;
    // Subset of getPacingSkipStat() that came from trimming the stale backlog
    // instead of from consuming a second frame in one draw. The remaining
    // difference is the cadence estimate over-reaching.
    [[nodiscard]] size_t getTrimSkipStat() const;
    [[nodiscard]] size_t getScheduledHoldStat() const;
    [[nodiscard]] size_t getMaxPushBurstStat() const;
    [[nodiscard]] size_t getLocalClockPacedFrameStat() const;
    [[nodiscard]] size_t getPlayoutResyncStat() const;
    [[nodiscard]] double getEstimatedSourceFps() const;

    // Playout latency: how long the frame handed to the renderer sat in the
    // queue first. Averaged and peaked over the window since the previous call,
    // so a 1 Hz reader measures the last second instead of the whole session.
    void takePresentWaitStats(double& avgMs, double& peakMs);

    // Wait of the frame most recently handed to the renderer, without consuming
    // it. This is the part of the latency the buffer setting controls, so it is
    // the number to show live while the player tunes that setting.
    [[nodiscard]] double getPresentWaitMs() const;

    void cleanup();

private:
    friend class AVFrameHolder;
    AVFrame* acquireFrameLocked();
    bool pushTransferredLocked(AVFrame* item);
    void recordArrivalLocked(std::chrono::steady_clock::time_point now);
    void resetArrivalRateEstimatorLocked();
    void updateAdaptiveTargetLocked(std::chrono::steady_clock::time_point now,
                                    bool underrun);
    void trimToPlayoutWindowLocked();
    // The frame handed to the renderer does not go back into the free pool right
    // away. On the zero-copy path that frame is the NVTegra buffer the GPU is
    // sampling, and the plain submit path has no fence telling us when it is
    // done with it; the delayed release puts a few draws between presenting a
    // buffer and letting the decoder write into it again.
    void recyclePresentedFrameLocked(AVFrame*& frame);
    static constexpr size_t kPresentedRecycleDelay = 2;
    std::queue<AVFrame*> pendingRecycle;
    size_t limit = 0;
    std::queue<AVFrame*> queue;
    // Arrival stamp per queued frame, kept in lockstep with `queue` on every
    // dequeue path so the wait of a presented frame can be measured.
    std::queue<std::chrono::steady_clock::time_point> arrivalTimes;
    std::queue<AVFrame*> freeQueue;
    AVFrame* bufferFrame = nullptr;
    bool transferOwnership = false;
    size_t targetBufferedFrames = 0;
    // Adaptive depth: the configured depth is the ceiling, one frame below it
    // is the floor. See updateAdaptiveTargetLocked().
    size_t baselineTargetFrames = 0;
    size_t minTargetFrames = 0;
    size_t targetReduceBackoff = 1;
    std::chrono::steady_clock::time_point nextTargetReduceTime{};
    int streamFps = 0;
    std::chrono::nanoseconds adaptiveFrameInterval{0};
    std::chrono::steady_clock::time_point lastDraw{};
    std::chrono::nanoseconds averageDrawInterval{0};
    std::chrono::steady_clock::time_point arrivalWindowStart{};
    std::chrono::steady_clock::time_point lastArrival{};
    size_t arrivalWindowFrames = 0;
    size_t arrivalRateSamples = 0;
    double estimatedSourceFps = 0.0;
    double frameCredit = 0.0;
    bool drawClockStarted = false;
    bool arrivalClockStarted = false;
    bool startupBuffering = true;
    bool playoutResyncNeeded = true;
    mutable std::mutex m_mutex;
    size_t fakeFrameUsedStat = 0;
    size_t framesDroppedStat = 0;
    size_t emptyQueueStat = 0;
    size_t rebufferHoldStat = 0;
    size_t overflowDropStat = 0;
    size_t pacingSkipStat = 0;
    size_t trimSkipStat = 0;
    size_t scheduledHoldStat = 0;
    size_t pushesSincePop = 0;
    size_t maxPushBurstStat = 0;
    size_t localClockPacedFrameStat = 0;
    size_t playoutResyncStat = 0;
    double presentWaitMs = 0.0;
    double presentWaitSumStat = 0.0;
    size_t presentWaitSamplesStat = 0;
    double peakPresentWaitStat = 0.0;
};

class AVFrameHolder : public Singleton<AVFrameHolder> {
  public:
    void push(AVFrame* frame) {
        pushedTotal.fetch_add(1, std::memory_order_relaxed);
        lastPushNs.store(nowNs(), std::memory_order_relaxed);
        m_frame_queue.push(frame);
    }

    void pushTransferred(AVFrame* frame) {
        pushedTotal.fetch_add(1, std::memory_order_relaxed);
        lastPushNs.store(nowNs(), std::memory_order_relaxed);
        m_frame_queue.pushTransferred(frame);
    }

    // Monotonic count of frames handed over by the decoder. A sleep/wake cycle
    // leaves stale frames sitting in the queue, so queue size alone cannot tell
    // a recovered stream from a frozen one: only this counter moves when the
    // decoder is actually running again.
    [[nodiscard]] uint64_t getPushedTotal() const {
        return pushedTotal.load(std::memory_order_relaxed);
    }

    // Time of the last push on the same clock the recovery flow uses. The
    // counter alone cannot tell a live stream from the tail of the packets that
    // were already queued before the console slept: waking up drains those into
    // a short burst and then goes quiet. Continuity, not content, is what
    // separates the two, and this timestamp is the cheap way to measure it.
    [[nodiscard]] int64_t getLastPushNs() const {
        return lastPushNs.load(std::memory_order_relaxed);
    }

    static int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // Deliberately a template rather than std::function: the renderer's lambda
    // captures more than libstdc++ keeps in its small object buffer, so every
    // drawn frame paid a heap allocation for the call.
    template <typename Fn>
    void get(Fn&& fn) {
        auto frame = m_frame_queue.pop();

        if (frame) {
            fn(frame);
        }
    }

    AVFrame* acquireWriteFrame() {
        return m_frame_queue.acquireWriteFrame();
    }

    void recycleWriteFrame(AVFrame*& frame) {
        m_frame_queue.recycleWriteFrame(frame);
    }

    void prepare(int streamFps, bool transferOwnership = false) {
        m_frame_queue.configure(Settings::instance().frames_queue_size(),
                                streamFps, transferOwnership);
    }

    void cleanup() {
        m_frame_queue.cleanup();
    }

    [[nodiscard]] size_t getFakeFrameStat() const { return m_frame_queue.getFakeFrameUsage(); }
    [[nodiscard]] size_t getFrameDropStat() const { return m_frame_queue.getFramesDropStat(); }
    [[nodiscard]] size_t getFrameQueueSize() const { return m_frame_queue.size(); }
    [[nodiscard]] size_t getFrameQueueTargetDepth() const { return m_frame_queue.targetDepth(); }
    [[nodiscard]] size_t getFrameQueueCapacity() const { return m_frame_queue.capacity(); }
    [[nodiscard]] size_t getFrameQueueEmptyStat() const { return m_frame_queue.getEmptyQueueStat(); }
    [[nodiscard]] size_t getFrameQueueRebufferHoldStat() const { return m_frame_queue.getRebufferHoldStat(); }
    [[nodiscard]] size_t getFrameQueueOverflowDropStat() const { return m_frame_queue.getOverflowDropStat(); }
    [[nodiscard]] size_t getFrameQueuePacingSkipStat() const { return m_frame_queue.getPacingSkipStat(); }
    [[nodiscard]] size_t getFrameQueueTrimSkipStat() const { return m_frame_queue.getTrimSkipStat(); }
    [[nodiscard]] size_t getFrameQueueScheduledHoldStat() const { return m_frame_queue.getScheduledHoldStat(); }
    [[nodiscard]] size_t getFrameQueueMaxPushBurstStat() const { return m_frame_queue.getMaxPushBurstStat(); }
    [[nodiscard]] size_t getFrameQueueLocalClockPacedFrameStat() const { return m_frame_queue.getLocalClockPacedFrameStat(); }
    [[nodiscard]] size_t getFrameQueuePlayoutResyncStat() const { return m_frame_queue.getPlayoutResyncStat(); }
    [[nodiscard]] double getFrameQueueEstimatedSourceFps() const { return m_frame_queue.getEstimatedSourceFps(); }
    void takeFrameQueuePresentWaitStats(double& avgMs, double& peakMs) {
        m_frame_queue.takePresentWaitStats(avgMs, peakMs);
    }
    [[nodiscard]] double getFrameQueuePresentWaitMs() const { return m_frame_queue.getPresentWaitMs(); }

  private:
    // Written by the decode thread, read by the UI thread.
    std::atomic<uint64_t> pushedTotal{0};
    std::atomic<int64_t> lastPushNs{0};
    AVFrameQueue m_frame_queue;
};
