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
    /**
     * Ends the stream when the console suspends, instead of carrying decoder
     * and GPU state across it.
     *
     * Losing applet focus on Switch means the console is going to sleep or the
     * HOME menu took over. Both end the stream, which is what Moonlight on
     * Android does at the same point: Game.onStop() calls stopConnection()
     * then finish().
     *
     * Carrying the session across a suspend is issue #306: the console hangs
     * after waking, with no fatal error and not even a crash report, because
     * the OS tears down the graphics and network services underneath a
     * process that keeps running. There is no stale state left to get wrong
     * once the stream is gone.
     */
    void onWindowFocusChanged(bool focused);
    brls::Event<bool>::Subscription windowFocusSubscription;

    /**
     * Set by onWindowFocusChanged(), acted on in draw().
     *
     * The focus callback runs inside Event<bool>::fire, which iterates the
     * very list it lives in (event.hpp: `for (Callback cb : this->callbacks)`).
     * Calling terminate() from there dismisses this view, and the destructor
     * unsubscribes, which erases the node fire() is standing on. So the
     * callback only records the intent; draw() performs it.
     */
    bool pendingSuspendTerminate = false;

    /**
     * How many window-focus subscriptions this class holds.
     *
     * One streaming view exists at a time, so this is 1 while streaming and 0
     * otherwise; anything else is a leak. Logged on both sides because a
     * leaked subscription says nothing until it fires on a destroyed object.
     */
    static int focusSubscriptionCount;

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
    brls::Event<brls::KeyState>::Subscription keysSubscription;
    int touchScrollCounter = 0;
    size_t bottombarDelayTask = -1;
    bool m_use_hdr = false;
    TwoFingerScrollGestureRecognizer* scrollTouchRecognizer = nullptr;

    void handleInput();
    void sampleGamepadInput();
    void handleOverlayCombo();
    void handleMouseInputCombo();
    void addKeyboard();
    void removeKeyboard();
};
