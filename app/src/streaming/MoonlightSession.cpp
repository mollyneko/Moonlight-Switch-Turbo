#include "MoonlightSession.hpp"
#include "AVFrameHolder.hpp"
#include "GameStreamClient.hpp"
#include "InputManager.hpp"
#include "Settings.hpp"
#include "borealis.hpp"
#include <string.h>

#if defined(PLATFORM_IOS) || defined(PLATFORM_VISIONOS)
extern void getWindowSize(int* w, int* h);
#endif

using namespace brls;

int m_video_format;
static MoonlightSession* m_active_session = nullptr;
static MoonlightSessionDecoderAndRenderProvider* m_provider = nullptr;


MoonlightSession* MoonlightSession::activeSession() {
    return m_active_session;
}

void MoonlightSession::set_provider(
    MoonlightSessionDecoderAndRenderProvider* provider) {
    m_provider = provider;
}

MoonlightSession::MoonlightSession(const std::string& address, int app_id) {
    m_address = address;
    m_app_id = app_id;
    m_active_session = this;

    m_video_decoder = m_provider->video_decoder();
    m_video_renderer = m_provider->video_renderer();
    m_audio_renderer = m_provider->audio_renderer();
}

MoonlightSession::~MoonlightSession() {
    if (m_video_decoder) {
        delete m_video_decoder;
    }

    if (m_video_renderer) {
        delete m_video_renderer;
    }

    if (m_audio_renderer) {
        delete m_audio_renderer;
    }

    m_active_session = nullptr;
}

// MARK: Connection callbacks

static const char* stages[] = {"STAGE_NONE",
                               "STAGE_PLATFORM_INIT",
                               "STAGE_NAME_RESOLUTION",
                               "STAGE_RTSP_HANDSHAKE",
                               "STAGE_CONTROL_STREAM_INIT",
                               "STAGE_VIDEO_STREAM_INIT",
                               "STAGE_AUDIO_STREAM_INIT",
                               "STAGE_INPUT_STREAM_INIT",
                               "STAGE_CONTROL_STREAM_START",
                               "STAGE_VIDEO_STREAM_START",
                               "STAGE_AUDIO_STREAM_START",
                               "STAGE_INPUT_STREAM_START"};

void MoonlightSession::connection_stage_starting(int stage) {
    brls::Logger::info("MoonlightSession: Starting: {}", stages[stage]);
}

void MoonlightSession::connection_stage_complete(int stage) {
    brls::Logger::info("MoonlightSession: Complete: {}", stages[stage]);
}

void MoonlightSession::connection_stage_failed(int stage, int error_code) {
    brls::Logger::error("MoonlightSession: Failed: {} with error code: {}", stages[stage], error_code);
}

void MoonlightSession::connection_started() {
    brls::Logger::info("MoonlightSession: Connection started");
    if (!m_active_session)
        return;

    m_active_session->m_stop_requested = false;
    m_active_session->m_is_active = true;
}

void MoonlightSession::connection_terminated(int error_code) {
    brls::Logger::info("MoonlightSession: Connection terminated with code: {}", error_code);

    if (!m_active_session)
        return;

    if (m_active_session->m_stop_requested) {
        brls::Logger::info("MoonlightSession: Termination acknowledged after stop request");
        m_active_session->m_is_active = false;
        m_active_session->m_is_terminated = true;
        return;
    }

    if (error_code != 0) {
        brls::Logger::info("MoonlightSession: Reconnection attempt");

        // Connection is already terminated here; avoid toggling the user stop flag.
        LiStopConnection();

        m_active_session->start([](const GSResult<bool>& result) {
            if (result.isSuccess()) {
                brls::Logger::info("MoonlightSession: Reconnected");
            } else {
                brls::Logger::info("MoonlightSession: Reconnection failed");
                if (m_active_session) {
                    m_active_session->m_is_active = false;
                    m_active_session->m_is_terminated = true;
                }
            }
        }, m_active_session->m_is_sunshine);
        return;
    }

    m_active_session->m_is_active = false;
    m_active_session->m_is_terminated = true;
}

void MoonlightSession::connection_log_message(const char* format, ...) {
    va_list arglist;
    va_start(arglist, format);
    int size = vsnprintf(NULL, 0, format, arglist);
    char buffer[size];
    vsnprintf(buffer, size, format, arglist);
    va_end(arglist);

    brls::Logger::info(fmt::runtime(std::string(buffer)));
}

void MoonlightSession::connection_rumble(unsigned short controller,
                                         unsigned short lowFreqMotor,
                                         unsigned short highFreqMotor) {
    MoonlightInputManager::instance().handleRumble(controller, lowFreqMotor,
                                                   highFreqMotor);
}


void MoonlightSession::connection_rumble_triggers(uint16_t controllerNumber, 
                                                  uint16_t leftTriggerMotor, 
                                                  uint16_t rightTriggerMotor) 
{
    // MoonlightInputManager::instance().handleRumbleTriggers(controllerNumber, leftTriggerMotor, rightTriggerMotor);                                                
}

void MoonlightSession::connection_status_update(int connection_status) {
    if (m_active_session) {
        const bool is_poor = connection_status == CONN_STATUS_POOR;
        if (is_poor != m_active_session->m_connection_status_is_poor) {
            brls::Logger::warning(
                "MoonlightSession: connection quality is now {}",
                is_poor ? "POOR" : "OK");
        }
        m_active_session->m_connection_status_is_poor = is_poor;
    }
}

void MoonlightSession::connection_set_hdr_mode(bool use_hdr) {
    if (m_active_session) {
        m_active_session->m_use_hdr = use_hdr;
    }
}

// MARK: Video decoder callbacks

int MoonlightSession::video_decoder_setup(int video_format, int width,
                                          int height, int redraw_rate,
                                          void* context, int dr_flags) {
    m_video_format = video_format;
    if (m_active_session && m_active_session->m_video_decoder) {
        return m_active_session->m_video_decoder->setup(
            video_format, width, height, redraw_rate, context, dr_flags);
    }
    return DR_OK;
}

void MoonlightSession::video_decoder_start() {
    if (m_active_session && m_active_session->m_video_decoder) {
        m_active_session->m_video_decoder->start();
    }
}

void MoonlightSession::video_decoder_stop() {
    if (m_active_session && m_active_session->m_video_decoder) {
        m_active_session->m_video_decoder->stop();
    }
}

void MoonlightSession::video_decoder_cleanup() {
    if (m_active_session && m_active_session->m_video_decoder) {
        m_active_session->m_video_decoder->cleanup();
    }
}

int MoonlightSession::video_decoder_submit_decode_unit(
    PDECODE_UNIT decode_unit) {
    if (m_active_session && m_active_session->m_video_decoder) {
        return m_active_session->m_video_decoder->submit_decode_unit(
            decode_unit);
    }
    return DR_OK;
}

// MARK: Audio callbacks

int MoonlightSession::audio_renderer_init(
    int audio_configuration, const POPUS_MULTISTREAM_CONFIGURATION opus_config,
    void* context, int ar_flags) {
    if (m_active_session && m_active_session->m_audio_renderer) {
        return m_active_session->m_audio_renderer->init(
            audio_configuration, opus_config, context, ar_flags);
    }
    return DR_OK;
}

void MoonlightSession::audio_renderer_start() {
    if (m_active_session && m_active_session->m_audio_renderer) {
        m_active_session->m_audio_renderer->start();
    }
}

void MoonlightSession::audio_renderer_stop() {
    if (m_active_session && m_active_session->m_audio_renderer) {
        m_active_session->m_audio_renderer->stop();
    }
}

void MoonlightSession::audio_renderer_cleanup() {
    if (m_active_session && m_active_session->m_audio_renderer) {
        m_active_session->m_audio_renderer->cleanup();
    }
}

void MoonlightSession::audio_renderer_decode_and_play_sample(
    char* sample_data, int sample_length) {
    if (m_active_session && m_active_session->m_audio_renderer) {
        m_active_session->m_audio_renderer->decode_and_play_sample(
            sample_data, sample_length);
    }
}

// MARK: MoonlightSession

void MoonlightSession::start(ServerCallback<bool> callback, bool is_sunshine) {
    m_is_sunshine = is_sunshine;
    m_stop_requested = false;
    m_is_terminated = false;

    LiInitializeStreamConfiguration(&m_config);

    int resolution = Settings::instance().resolution();
    int h = resolution;
    int w = h * 16 / 9;
    if (resolution == -1) {
#if defined(PLATFORM_IOS) || defined(PLATFORM_VISIONOS)
        getWindowSize(&w, &h);
#else
        h = Application::windowHeight;
        w = Application::windowWidth;
#endif

        int nativeResolutionScale = Settings::instance().native_resolution_scale();
        if (nativeResolutionScale != 100 && w > 0 && h > 0) {
            w = (w * nativeResolutionScale + 50) / 100;
            h = (h * nativeResolutionScale + 50) / 100;
        }
    }

    // Prohibit odd values rounding up
    if ((h & 1) == 1) {
        h += 1;
    }

    if ((w & 1) == 1) {
        w += 1;
    }

    m_config.width = w;
    m_config.height = h;
    m_config.fps = Settings::instance().fps();
    // The host paces its encoder against the refresh rate we can actually
    // present, in Hz * 100. Switch output is 60Hz in handheld and docked mode.
    m_config.clientRefreshRateX100 = 6000;
    m_config.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    m_config.packetSize = 1392;
    m_config.streamingRemotely = STREAM_CFG_AUTO;
    m_config.bitrate = Settings::instance().bitrate();
    m_config.encryptionFlags = m_is_sunshine ? ENCFLG_ALL : ENCFLG_VIDEO;

    switch (Settings::instance().video_codec()) {
    case H264:
        m_config.supportedVideoFormats = VIDEO_FORMAT_H264;
        break;
    case H265:
        m_config.supportedVideoFormats = VIDEO_FORMAT_H265;
            if (Settings::instance().request_hdr())
                m_config.supportedVideoFormats |= VIDEO_FORMAT_H265_MAIN10;
        break;
    case AV1:
        m_config.supportedVideoFormats = VIDEO_FORMAT_AV1_MAIN8;
            if (Settings::instance().request_hdr())
                m_config.supportedVideoFormats |= VIDEO_FORMAT_AV1_MAIN10;
        break;
    default:
        break;
    }

    brls::Logger::info(
        "MoonlightSession: requested {}x{} @{}fps, {} kbps, codec {}, hdr {}, "
        "queue {}, decoder_threads {}, post-processing: upscaling={} "
        "dithering={} rcas={}",
        w, h, m_config.fps, m_config.bitrate,
        getVideoCodecName(Settings::instance().video_codec()),
        Settings::instance().request_hdr() ? "on" : "off",
        Settings::instance().frames_queue_size(),
        Settings::instance().decoder_threads(),
        Settings::instance().upscaling() ? "on" : "off",
        Settings::instance().dithering() ? "on" : "off",
        Settings::instance().rcas() ? "on" : "off");

    LiInitializeConnectionCallbacks(&m_connection_callbacks);
    m_connection_callbacks.stageStarting = connection_stage_starting;
    m_connection_callbacks.stageComplete = connection_stage_complete;
    m_connection_callbacks.stageFailed = connection_stage_failed;
    m_connection_callbacks.connectionStarted = connection_started;
    m_connection_callbacks.connectionTerminated = connection_terminated;
    m_connection_callbacks.logMessage = connection_log_message;
    m_connection_callbacks.rumble = connection_rumble;
    m_connection_callbacks.rumbleTriggers = connection_rumble_triggers;
    m_connection_callbacks.connectionStatusUpdate = connection_status_update;
    m_connection_callbacks.setHdrMode = connection_set_hdr_mode;

    LiInitializeVideoCallbacks(&m_video_callbacks);
    m_video_callbacks.setup = video_decoder_setup;
    m_video_callbacks.start = video_decoder_start;
    m_video_callbacks.stop = video_decoder_stop;
    m_video_callbacks.cleanup = video_decoder_cleanup;
    m_video_callbacks.submitDecodeUnit = video_decoder_submit_decode_unit;

    if (m_video_decoder) {
        m_video_callbacks.capabilities = m_video_decoder->capabilities();
    }

    LiInitializeAudioCallbacks(&m_audio_callbacks);
    m_audio_callbacks.init = audio_renderer_init;
    m_audio_callbacks.start = audio_renderer_start;
    m_audio_callbacks.stop = audio_renderer_stop;
    m_audio_callbacks.cleanup = audio_renderer_cleanup;
    m_audio_callbacks.decodeAndPlaySample =
        audio_renderer_decode_and_play_sample;

    if (m_audio_renderer) {
        m_audio_callbacks.capabilities = m_audio_renderer->capabilities();
    }

    GameStreamClient::instance().start(
        m_address, m_config, m_app_id, [this, callback](auto result) {
            if (result.isSuccess()) {
                m_config = result.value();
                brls::Logger::info(
                    "MoonlightSession: negotiated {}x{} @ {} fps, {} kbps, "
                    "formats 0x{:x}, refresh_x100 {}, audio 0x{:x}, remote {}",
                    m_config.width, m_config.height, m_config.fps,
                    m_config.bitrate, m_config.supportedVideoFormats,
                    m_config.clientRefreshRateX100, m_config.audioConfiguration,
                    (int) m_config.streamingRemotely);
                brls::async([this, callback]() mutable {
                    auto m_data =
                        GameStreamClient::instance().server_data(m_address);

                    int result = LiStartConnection(
                        &m_data.serverInfo, &m_config, &m_connection_callbacks,
                        &m_video_callbacks, &m_audio_callbacks, NULL, 0, NULL, 0);

                    if (result != 0) {
                        LiStopConnection();
                        callback(
                            GSResult<bool>::failure("error/stream_start"_i18n));
                    } else {
                        callback(GSResult<bool>::success(true));
                    }
                });
            } else {
                brls::Logger::error(
                    "MoonlightSession: Failed to start stream: {}",
                    result.error().c_str());
                callback(GSResult<bool>::failure(result.error()));
            }
        });
}

void MoonlightSession::stop(int terminate_app) {
    if (m_stop_requested)
        return;

    m_stop_requested = true;

    if (terminate_app) {
        GameStreamClient::instance().quit(m_address, [](auto _) {});
    }

    LiStopConnection();
}

void MoonlightSession::restart() {
    LiStopConnection();

    start([](const GSResult<bool>& result) {
        if (result.isSuccess()) {
            brls::Logger::info("MoonlightSession: Reconnected");
        } else {
            brls::Logger::info("MoonlightSession: Reconnection failed");
            if (m_active_session) {
                m_active_session->m_is_active = false;
                m_active_session->m_is_terminated = true;
            }
        }
    }, m_active_session->m_is_sunshine);
}

void MoonlightSession::draw(NVGcontext* vg, int width, int height) {
    if (m_video_decoder && m_video_renderer) {
        AVFrameHolder::instance().get(
            [this, vg, width, height](AVFrame* frame) {
                m_video_renderer->draw(vg, width, height, frame, m_video_format);
            });

        const uint64_t now = LiGetMillis();
        if (m_last_stats_update_ms == 0 || now - m_last_stats_update_ms >= 250) {
            m_session_stats.video_decode_stats =
                *m_video_decoder->video_decode_stats();
            m_session_stats.video_render_stats =
                *m_video_renderer->video_render_stats();
            m_last_stats_update_ms = now;
        }

        // Snapshot the session health once a second so a stutter can be traced
        // to network, decode, render or pacing after the fact.
        if (m_last_telemetry_ms == 0 || now - m_last_telemetry_ms >= 1000) {
            m_last_telemetry_ms = now;
            log_telemetry();
        }
    }
}

namespace {
// Counters only grow within a session, but the guard keeps a restarted session
// from producing a nonsense negative delta.
template <typename T> T counterDelta(T current, T previous) {
    return current >= previous ? static_cast<T>(current - previous) : T(0);
}
} // namespace

void MoonlightSession::log_telemetry() {
    if (!m_video_decoder || !m_video_renderer)
        return;

    const auto& dec = m_session_stats.video_decode_stats;
    const auto& ren = m_session_stats.video_render_stats;
    auto& holder = AVFrameHolder::instance();

    const size_t pacing_skip = holder.getFrameQueuePacingSkipStat();
    const size_t trim_skip = holder.getFrameQueueTrimSkipStat();
    const size_t queue_empty = holder.getFrameQueueEmptyStat();
    const size_t overflow = holder.getFrameQueueOverflowDropStat();
    const size_t resync = holder.getFrameQueuePlayoutResyncStat();

    const size_t d_pacing_skip = counterDelta(pacing_skip, m_prev_pacing_skip);
    const size_t d_trim_skip = counterDelta(trim_skip, m_prev_trim_skip);
    const size_t d_queue_empty = counterDelta(queue_empty, m_prev_queue_empty);
    const size_t d_overflow = counterDelta(overflow, m_prev_overflow);
    const size_t d_resync = counterDelta(resync, m_prev_resync);
    const uint32_t d_net_dropped =
        counterDelta(dec.network_dropped_frames, m_prev_net_dropped);

    m_prev_pacing_skip = pacing_skip;
    m_prev_trim_skip = trim_skip;
    m_prev_queue_empty = queue_empty;
    m_prev_overflow = overflow;
    m_prev_resync = resync;
    m_prev_net_dropped = dec.network_dropped_frames;

    uint32_t rtt = 0;
    uint32_t rtt_variance = 0;
    const bool has_rtt = LiGetEstimatedRttInfo(&rtt, &rtt_variance);

    // Where the lag actually goes. Until now the log only carried RTT, so a
    // "it feels laggy" report could only be guessed at. These three numbers
    // separate host encode time, network round trip and client buffering.
    double queue_wait = 0.0;
    double queue_wait_peak = 0.0;
    holder.takeFrameQueuePresentWaitStats(queue_wait, queue_wait_peak);
    const double host_enc = dec.current_host_latency;
    const double network_one_way = has_rtt ? rtt / 2.0 : 0.0;
    const double est_glass = host_enc + network_one_way + queue_wait +
                             dec.current_decoding_time + ren.rendering_time;

    brls::Logger::info(
        "TELEMETRY host_fps={:.2f} recv_fps={:.2f} decode_fps={:.2f} "
        "render_fps={:.2f} net_dropped={} recv_ms={:.2f} decode_ms={:.2f} "
        "dec_delay_ms={:.2f} render_ms={:.2f} queue={}/{} cap={} "
        "fake={} dropped={} skip_trim={} empty={} overflow={} resync={} est_src_fps={:.2f} "
        "rtt_ms={}{} poor={}",
        dec.current_host_fps, dec.current_received_fps, dec.current_decoded_fps,
        ren.rendered_fps, dec.network_dropped_frames, dec.current_receive_time,
        dec.current_decoding_time, dec.current_decoder_delay,
        ren.rendering_time, holder.getFrameQueueSize(),
        holder.getFrameQueueTargetDepth(), holder.getFrameQueueCapacity(),
        holder.getFakeFrameStat(), holder.getFrameDropStat(), trim_skip,
        queue_empty, overflow, resync,
        holder.getFrameQueueEstimatedSourceFps(),
        has_rtt ? rtt : 0, has_rtt ? "" : "(n/a)",
        m_connection_status_is_poor ? "yes" : "no");

    brls::Logger::info(
        "LATENCY host_enc_ms={:.1f} net_oneway_ms={:.1f} queue_wait_ms={:.1f} "
        "queue_wait_peak_ms={:.1f} decode_ms={:.1f} render_ms={:.1f} "
        "est_glass_ms={:.1f}",
        host_enc, network_one_way, queue_wait, queue_wait_peak,
        dec.current_decoding_time, ren.rendering_time, est_glass);

    m_session_stats.queue_wait_ms = static_cast<float>(queue_wait);
    m_session_stats.estimated_glass_ms = static_cast<float>(est_glass);

    // Per-second event lines. These turn a cumulative counter into a timestamp
    // that can be lined up with a stutter the player actually saw.
    if (d_net_dropped > 0)
        brls::Logger::warning(
            "STUTTER network: dropped {} frame(s) in the last second",
            d_net_dropped);
    if (d_queue_empty > 0)
        brls::Logger::warning(
            "STUTTER decode queue: starved {} time(s) in the last second",
            d_queue_empty);
    if (d_overflow > 0)
        brls::Logger::warning(
            "STUTTER decode queue: overflowed, discarded {} frame(s)", d_overflow);
    if (d_pacing_skip > 0) {
        // Split the two reasons a frame gets discarded: a genuine stale backlog
        // being trimmed, versus the cadence estimate over-reaching and asking
        // for more than one frame in a single draw.
        const size_t cadenceSkips =
            d_pacing_skip > d_trim_skip ? d_pacing_skip - d_trim_skip : 0;
        brls::Logger::warning(
            "PACING: skipped {} frame(s) to keep playback in sync "
            "(backlog {}, cadence {})",
            d_pacing_skip, d_trim_skip, cadenceSkips);
    }
    if (d_resync > 0)
        brls::Logger::warning(
            "PACING: resynced playback {} time(s) after missing a deadline",
            d_resync);

    // Every five seconds, name the dominant bottleneck so the log can be read at
    // a glance instead of by comparing samples by hand.
    if (++m_telemetry_ticks % 5 == 0) {
        const char* verdict = "healthy";
        if (d_net_dropped > 0 || m_connection_status_is_poor ||
            (has_rtt && rtt > 25))
            verdict = "NETWORK (packet loss or high latency)";
        else if (ren.rendering_time > 12.0f)
            verdict = "RENDER (GPU frame time close to the budget)";
        else if (dec.current_decoding_time > 8.0f ||
                 dec.current_decoder_delay > 6.0f)
            verdict = "DECODE";
        else if (d_queue_empty > 0 || d_resync > 0 || d_pacing_skip > 1)
            verdict = "PACING (host frame arrival is uneven)";

        brls::Logger::info(
            "VERDICT: {} | render {:.1f}ms, decode {:.1f}ms, rtt {}ms, "
            "est latency {:.0f}ms",
            verdict, ren.rendering_time, dec.current_decoding_time,
            has_rtt ? static_cast<int>(rtt) : -1, est_glass);
    }
}
