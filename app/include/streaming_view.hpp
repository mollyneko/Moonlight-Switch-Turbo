//
//  streaming_view.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 27.05.2021.
//

#pragma once

#include "gestures/fingers_gesture_recognizer.hpp"
#include "keyboard_view.hpp"
#include "loading_overlay.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include <cstdint>
#include <optional>
#include <chrono>
#include "GameStreamClient.hpp"
#include "MoonlightSession.hpp"
#include "two_finger_scroll_recognizer.hpp"

class StreamingView : public brls::Box {
  public:
    StreamingView(const Host& host, const AppInfo& app);
    ~StreamingView();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;
    void onLayout() override;

    void terminate(bool terminateApp);

    bool draw_stats = false;

    Host getHost() { return host; }

    AppInfo getApp() { return app; }

  private:
    // A sleep/wake cycle leaves the sockets, NVDEC and audren in a state nobody
    // told this process about. Instead of guessing from how long the console was
    // away, the view watches the frames: if the decoder comes back on its own
    // nothing is touched, and only a stream that really stopped is rebuilt.
    enum class Recovery {
        Idle,       // Streaming normally.
        Probing,    // The window came back; waiting to see whether frames resume.
        Recovering, // The session is being torn down and renegotiated.
    };

    Host host;
    AppInfo app;
    MoonlightSession* session = nullptr;
    LoadingOverlay* loader = nullptr;
    Box* keyboardHolder = nullptr;
    KeyboardView* keyboard = nullptr;
    bool blocked = false;
    bool terminated = false;
    bool tempInputLock = false;
    // Used to tell a sleep/wake cycle apart from a normal frame: see draw().
    std::chrono::steady_clock::time_point lastDrawTime =
        std::chrono::steady_clock::now();

    Recovery recovery = Recovery::Idle;
    // Set by the applet focus hook, which is the only wake notification an
    // application gets. Consumed by updateRecovery().
    bool wakeDetected = false;
    uint64_t probeBaseFrames = 0;
    uint64_t recoveryBaseFrames = 0;
    std::chrono::steady_clock::time_point recoveryPhaseStart;
    brls::Event<bool>::Subscription windowFocusSubscription;

    brls::Event<brls::KeyState>::Subscription keysSubscription;
    int touchScrollCounter = 0;
    size_t bottombarDelayTask = -1;
    bool m_use_hdr = false;
    TwoFingerScrollGestureRecognizer* scrollTouchRecognizer = nullptr;

    // Returns true when the frame has been consumed by the recovery flow and
    // the session must not be drawn this time.
    bool updateRecovery(std::chrono::steady_clock::time_point now,
                        std::chrono::steady_clock::duration sinceLastDraw);
    void beginRecovery(std::chrono::steady_clock::time_point now);
    void abandonRecovery();

    void handleInput();
    void sampleGamepadInput();
    void handleOverlayCombo();
    void handleMouseInputCombo();
    void addKeyboard();
    void removeKeyboard();
};
