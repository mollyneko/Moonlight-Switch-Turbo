#pragma once

#include <Limelight.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct VideoDecodeStats {
    // NOT TO USE, INTERMEDIATE VALUES
    uint32_t current_received_frames;
    uint32_t current_decoded_frames;
    uint32_t total_frames;
    uint32_t network_dropped_frames;
    uint32_t current_reassembly_time;
    uint32_t current_decode_time;
  uint32_t current_decoder_delay_time;
    // Host-side encode time, accumulated in the 1/10 ms units the host reports
    // it in. Samples are counted separately because the host sends 0 for frames
    // it repeated, and those zeroes must not drag the average down.
    uint32_t current_host_latency_time;
    uint32_t current_host_latency_samples;
    uint32_t total_received_frames;
    uint32_t total_decoded_frames;
    uint32_t total_reassembly_time;
    uint32_t total_decode_time;
  uint32_t total_decoder_delay_time;

    float current_host_fps;
    float current_received_fps;
    float current_decoded_fps;

    float current_receive_time;
    float current_decoding_time;
    float current_decoder_delay;
    // Average time the host spent capturing and encoding a frame, in ms. Zero
    // when the host does not report it.
    float current_host_latency;

    float session_receive_time;
    float session_decoding_time;
    float session_decoder_delay;

    uint64_t measurement_start_timestamp;
};

class IFFmpegVideoDecoder {
  public:
    virtual ~IFFmpegVideoDecoder()= default;
    virtual int setup(int video_format, int width, int height, int redraw_rate,
                      void* context, int dr_flags) = 0;
    virtual void start(){};
    virtual void stop(){};
    virtual void cleanup() = 0;
    virtual int submit_decode_unit(PDECODE_UNIT decode_unit) = 0;
    virtual int capabilities() const = 0;
    virtual VideoDecodeStats* video_decode_stats() = 0;
};
