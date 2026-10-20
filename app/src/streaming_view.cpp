//
//  streaming_view.cpp
//  Moonlight
//
//  Created by Даниил Виноградов on 27.05.2021.
//

#ifdef __SWITCH__
#include <borealis/platforms/switch/switch_input.hpp>
#endif

#include "streaming_view.hpp"
#include "AVFrameHolder.hpp"
#include "InputManager.hpp"
#include "click_gesture_recognizer.hpp"
#include "helper.hpp"
#include "ingame_overlay_view.hpp"
#include "streaming_input_overlay.hpp"
#include "two_finger_scroll_recognizer.hpp"
#include <Limelight.h>
#include <chrono>
#include <nanovg.h>

#if defined(__SDL3__)
#include <SDL3/SDL.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#endif

using namespace brls;

#ifdef PLATFORM_TVOS
extern void updatePreferredDisplayMode(bool streamActive);
#endif

void setBottomBarStatus(const char *value) {
#if defined(__SDL2__) || defined(__SDL3__)
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, value);
#endif
}

void overrideButtonsIfNeeded(bool value) {
#ifdef PLATFORM_SWITCH
    ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setScreenshotButtonOverrideMode(ButtonOverrideMode::NONE);
    ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setHomeButtonOverrideMode(ButtonOverrideMode::NONE);
    if (!value) return;

    switch (Settings::instance().get_overlay_system_button()) {
        case ButtonOverrideType::NONE: break;
        case ButtonOverrideType::HOME:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setHomeButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
            break;  
        case ButtonOverrideType::SCREENSHOT:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setScreenshotButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
            break;
    }

    switch (Settings::instance().get_guide_system_button()) {
        case ButtonOverrideType::NONE: break;
        case ButtonOverrideType::HOME:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setHomeButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
            break;  
        case ButtonOverrideType::SCREENSHOT:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setScreenshotButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
            break;
    }
#endif
}

StreamingView::StreamingView(const Host& host, const AppInfo& app) : host(host), app(app) {
    Application::getPlatform()->disableScreenDimming(true);

    setFocusable(true);
    setHideHighlight(true);
    loader = new LoadingOverlay(this);

    keyboardHolder = new Box(Axis::COLUMN);
    keyboardHolder->detach();
    keyboardHolder->setJustifyContent(JustifyContent::FLEX_END);
    keyboardHolder->setAlignItems(AlignItems::STRETCH);
    addView(keyboardHolder);

    session = new MoonlightSession(host.preferred_address(), app.app_id);

    // The only wake notification an application gets: the applet focus hook
    // behind this event fires when the console comes back. It also fires for
    // the HOME menu, which is harmless -- a stream that kept running keeps
    // pushing frames, so the probe drops straight back to Idle.
    windowFocusSubscription =
        Application::getWindowFocusChangedEvent()->subscribe([this](bool focused) {
            if (focused && !terminated && recovery != Recovery::Recovering)
                wakeDetected = true;
        });

#ifdef PLATFORM_TVOS
        updatePreferredDisplayMode(true);
#endif

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN](GSResult<SERVER_DATA> result) {
            ASYNC_RELEASE
            if (!result.isSuccess()) {
                showError(result.error(), [this]() { terminate(false); });
                return;
            }

            session->set_address(
                GameStreamClient::instance().active_address(this->host));

            ASYNC_RETAIN
            session->start([ASYNC_TOKEN](GSResult<bool> result) {
                ASYNC_RELEASE

                loader->setHidden(true);
                if (!result.isSuccess()) {
                    brls::Logger::error(
                        "StreamingView: stream failed to start: {}",
                        result.error());
                    showError(result.error(), [this]() { terminate(false); });
                } else {
                    brls::Logger::info("StreamingView: stream started");
                }
            }, result.value().isSunshine());
        });

    MoonlightInputManager::instance().reloadButtonMappingLayout();

    static bool lMouseKeyGate = false;
    static bool lMouseKeyUsed = false;
    addGestureRecognizer(new FingersGestureRecognizer([](){
                             return Settings::instance().get_keyboard_fingers();
                         }, [this] { addKeyboard(); }));

    addGestureRecognizer(
        new ClickGestureRecognizer(1, [](TapGestureStatus status) {
            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::END) {
                Logger::debug("Left mouse click");
                MoonlightInputManager::leftMouseClick();
                lMouseKeyGate = true;
                delay(200, [] { lMouseKeyGate = false; });
            }
        }));

    addGestureRecognizer(
        new ClickGestureRecognizer(2, [](TapGestureStatus status) {
            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::END) {
                Logger::debug("Right mouse click");
                MoonlightInputManager::rightMouseClick();
            }
        }));

    addGestureRecognizer(new PanGestureRecognizer(
        [this](PanGestureStatus status, Sound* sound) {
            static bool overlayTriggered = false;

            // Close keyboard by swiping outside of it
            if (status.state == brls::GestureState::START) {
                removeKeyboard();
                overlayTriggered = false;
            }

            // Open overlay by swipe from left screen corner
            bool hasControllers = Application::getPlatform()->getInputManager()->getControllersConnectedCount() > 0;
            if (!hasControllers && !overlayTriggered && status.state == brls::GestureState::STAY && status.startPosition.x < 100 && status.position.x > 200) {
                overlayTriggered = true;
                auto overlay = new IngameOverlay(this);
                Application::pushActivity(new Activity(overlay));
            }

            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::UNSURE && lMouseKeyGate) {
                lMouseKeyGate = false;
                lMouseKeyUsed = true;
            } else if (status.state == brls::GestureState::START) {
                if (lMouseKeyUsed) {
//                    Logger::debug("Pressed key at {}", status.state);
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS,
                                           BUTTON_MOUSE_LEFT);
                }
            } else if (status.state == brls::GestureState::STAY) {
                brls::RawMouseState mouseState;
                Application::getPlatform()->getInputManager()->updateMouseStates(&mouseState);
                // Dirty hack to not update pan if mouse left button is pressed, because pan gesture recognizer will append its speed with raw mouse value
                // Need to improve gesture recognizers to determine the input source and ignore it for mouse
                if (!mouseState.leftButton) {
                    MoonlightInputManager::instance().updateTouchScreenPanDelta(
                            status);
                }
            } else if (lMouseKeyUsed) {
//                Logger::debug("Release key at {}", status.state);
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,
                                       BUTTON_MOUSE_LEFT);
                lMouseKeyUsed = false;
            }
        },
        PanAxis::ANY));

    scrollTouchRecognizer = new TwoFingerScrollGestureRecognizer(
            [this](TwoFingerScrollState state) {
                if (Settings::instance().touchscreen_mouse_mode()) return;

                if (state.state == brls::GestureState::START)
                    this->touchScrollCounter = 0;

                int threshold = int(state.delta.y / 25);
                if (threshold != this->touchScrollCounter) {
                    Logger::debug("Scroll on: {}",
                                  threshold - this->touchScrollCounter);
                    int invert = Settings::instance().swap_mouse_scroll() ? -1 : 1;
                    char scrollCount = threshold - this->touchScrollCounter;
                    LiSendScrollEvent(scrollCount * invert);
                    this->touchScrollCounter = threshold;
                }
            });
    addGestureRecognizer(scrollTouchRecognizer);

    keysSubscription =
        Application::getPlatform()
            ->getInputManager()
            ->getKeyboardKeyStateChanged()
            ->subscribe([this, host, app](brls::KeyState state) {
                if (state.key == BRLS_KBD_KEY_ESCAPE) {
                    static std::chrono::high_resolution_clock::time_point
                        clock_counter;
                    static bool buttonState = false;
                    static bool used = false;

                    auto duration =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::high_resolution_clock::now() -
                            clock_counter);

                    if (!buttonState && state.pressed) {
                        buttonState = true;
                        clock_counter =
                            std::chrono::high_resolution_clock::now();
                    } else if (buttonState && !state.pressed) {
                        buttonState = false;
                        used = false;
                    } else if (buttonState && duration.count() >= 2 && !used) {
                        used = true;

                        auto overlay = new IngameOverlay(this);
                        Application::pushActivity(new Activity(overlay));
                    }
                }
            });
}

void StreamingView::onFocusGained() {
    Box::onFocusGained();

    MoonlightInputManager::instance().setInputEnabled(true);

    if (!blocked) {
        blocked = true;
        Application::blockInputs(true);
    }

    tempInputLock = true;
    ASYNC_RETAIN
    delay(300, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        this->tempInputLock = false;
    });

    Application::getPlatform()->getInputManager()->setPointerLock(true);

    overrideButtonsIfNeeded(true);
    setBottomBarStatus("1");

    scrollTouchRecognizer->forceReset();
}

void StreamingView::onFocusLost() {
    Box::onFocusLost();

    MoonlightInputManager::instance().setInputEnabled(false);
    MoonlightInputManager::instance().dropInput();

    if (blocked) {
        blocked = false;
        Application::unblockInputs();
    }

    removeKeyboard();
    Application::getPlatform()->getInputManager()->setPointerLock(false);

    overrideButtonsIfNeeded(false);
    setBottomBarStatus("2");

    if (bottombarDelayTask != -1)
        cancelDelay(bottombarDelayTask);
}

void StreamingView::draw(NVGcontext* vg, float x, float y, float width,
                         float height, Style style, FrameContext* ctx) {
    const auto now = std::chrono::steady_clock::now();
    const auto sinceLastDraw = now - lastDrawTime;
    lastDrawTime = now;

    // A sleep/wake cycle leaves the sockets, NVDEC and audren in a state nobody
    // told this process about, and streaming on top of them is what wedges the
    // console when the app is closed later. The recovery flow below rebuilds the
    // session, but only after the frames themselves say it is dead: see
    // updateRecovery().
    if (updateRecovery(now, sinceLastDraw))
        return;

    if (session->is_terminated()) {
        terminate(false);
        return;
    }

    // Sample and send input before submitting the video frame so input packets
    // leave as early as possible within this frame's window instead of waiting
    // for the render pass to finish. A second sample is taken at the very end
    // of this draw callback (see below): the two samples split the vblank
    // interval into shorter gaps, so a button press waits less for the next
    // sample than with a single sample per frame.
    if (!tempInputLock && session->is_active())
        handleInput();

    session->draw(vg, (int) width, (int) height);

    handleOverlayCombo();
    handleMouseInputCombo();

    if (session->connection_status_is_poor()) {
        nvgFontSize(vg, 20);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        nvgFontBlur(vg, 3);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgText(vg, 50, height - 28, "\uE140 Bad connection...", nullptr);

        nvgFontBlur(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgText(vg, 50, height - 28, "\uE140 Bad connection...", nullptr);
    }

    if (session->use_hdr() != m_use_hdr) {
        m_use_hdr = session->use_hdr();

#ifdef PLATFORM_TVOS
        updatePreferredDisplayMode(true);
#endif
    }

    if (draw_stats) {
        auto stats = session->session_stats();

        auto statistics = fmt::format(
                    "Estimated host PC frame rate: {:.{}f} FPS\n"
                        "Incoming frame rate from network: {:.{}f} FPS\n"
                        "Decoding frame rate: {:.{}f} FPS\n"
                        "Rendering frame rate: {:.{}f} FPS\n",
                    stats->video_decode_stats.current_host_fps, 2,
                    stats->video_decode_stats.current_received_fps, 2,
                    stats->video_decode_stats.current_decoded_fps, 2,
                    stats->video_render_stats.rendered_fps, 2);

        statistics += fmt::format("Frames dropped by your network connection: {}\n"
                                  "Average receive time: {:.{}f} | {:.{}f} ms\n"
                                  "Average decode time: {:.{}f} | {:.{}f} ms\n"
                                  "Average decoder delay: {:.{}f} | {:.{}f} ms\n"
                                  "Average rendering time: {:.{}f} ms\n",
                                  stats->video_decode_stats.network_dropped_frames,
                                  stats->video_decode_stats.current_receive_time, 2,
                                  stats->video_decode_stats.session_receive_time, 2,
                                  stats->video_decode_stats.current_decoding_time, 2,
                                  stats->video_decode_stats.session_decoding_time, 2,
                                  stats->video_decode_stats.current_decoder_delay, 2,
                                  stats->video_decode_stats.session_decoder_delay, 2,
                                  stats->video_render_stats.rendering_time, 2);

        if (stats->video_render_stats.gpu_timed_frames > 0) {
            statistics += fmt::format("Average GPU render time: {:.{}f} ms\n",
                                      stats->video_render_stats.gpu_rendering_time, 2);
        }

        if (stats->video_render_stats.post_processed_frames > 0) {
            statistics += fmt::format(
                "Average post-processing pass time: {:.{}f} ms (D:{:.{}f} | U:{:.{}f} | S:{:.{}f})\n",
                /* "Post-processed frames: {} / {}\n", */
                stats->video_render_stats.post_processing_time, 2,
                stats->video_render_stats.dithering_time, 2,
                stats->video_render_stats.upscaling_time, 2,
                stats->video_render_stats.sharpening_time, 2
                /*stats->video_render_stats.post_processed_frames, */
                /*stats->video_render_stats.rendered_frames*/);
        }

        // The buffer setting only trades latency for smoothness; these two
        // numbers are the trade, so show them rather than leaving the player to
        // guess whether a depth change helped.
        statistics += fmt::format(
            "Estimated end-to-end latency: {:.{}f} ms\n"
            "Queue wait (live) | host encoder: {:.{}f} | {:.{}f} ms\n",
            stats->estimated_glass_ms, 1,
            AVFrameHolder::instance().getFrameQueuePresentWaitMs(), 1,
            stats->video_decode_stats.current_host_latency, 1);

        statistics += fmt::format("Frames queue underflows | skipped: {} | {}\n"
                                  "Queue empty | startup holds: {} | {}\n"
                                  "Queue overflow | paced skips: {} | {}\n"
                                  "Scheduled frame holds: {}\n"
                                  "Frames presented by local clock: {}\n"
                                  "Playout resyncs | estimated source: {} | {:.2f} FPS\n"
                                  "Max pushes between draws: {}\n"
                                  "Frames queue depth | target | capacity: {} | {} | {}",
                                  AVFrameHolder::instance().getFakeFrameStat(),
                                  AVFrameHolder::instance().getFrameDropStat(),
                                  AVFrameHolder::instance().getFrameQueueEmptyStat(),
                                  AVFrameHolder::instance().getFrameQueueRebufferHoldStat(),
                                  AVFrameHolder::instance().getFrameQueueOverflowDropStat(),
                                  AVFrameHolder::instance().getFrameQueuePacingSkipStat(),
                                  AVFrameHolder::instance().getFrameQueueScheduledHoldStat(),
                                  AVFrameHolder::instance().getFrameQueueLocalClockPacedFrameStat(),
                                  AVFrameHolder::instance().getFrameQueuePlayoutResyncStat(),
                                  AVFrameHolder::instance().getFrameQueueEstimatedSourceFps(),
                                  AVFrameHolder::instance().getFrameQueueMaxPushBurstStat(),
                                  AVFrameHolder::instance().getFrameQueueSize(),
                                  AVFrameHolder::instance().getFrameQueueTargetDepth(),
                                  AVFrameHolder::instance().getFrameQueueCapacity());

        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgFontSize(vg, 20);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);

        nvgFontBlur(vg, 1);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgTextBox(vg, 20, 30, width, statistics.c_str(), nullptr);

        nvgFontBlur(vg, 0);
        nvgFillColor(vg, nvgRGBA(0, 255, 0, 255));
        nvgTextBox(vg, 20, 30, width, statistics.c_str(), nullptr);
    }

    Box::draw(vg, x, y, width, height, style, ctx);

    // Second input sample of this frame, taken as late as possible (this is
    // the last work done before the frame is presented). Only the gamepad is
    // sampled here; keyboard diffing and the bottom bar timer stay on the
    // single call above.
    if (!tempInputLock && session->is_active())
        sampleGamepadInput();
}

void StreamingView::addKeyboard() {
    if (keyboard)
        return;

    keyboard = new KeyboardView(false);
    keyboardHolder->addView(keyboard);
}

void StreamingView::removeKeyboard() {
    if (!keyboard)
        return;

    keyboard->removeFromSuperView();
    keyboard = nullptr;
    Application::giveFocus(this);
}

// MARK: Sleep/wake recovery

bool StreamingView::updateRecovery(std::chrono::steady_clock::time_point now,
                                   std::chrono::steady_clock::duration sinceLastDraw) {
    auto& frames = AVFrameHolder::instance();

    switch (recovery) {
        case Recovery::Idle: {
            if (session->is_terminated())
                return false; // draw() closes the view right below.

            const bool woke = wakeDetected;
            wakeDetected    = false;

            // The fallback watches the decoder, not the UI. Drawing keeps going
            // at 60 fps even when the stream is dead, so the draw gap never
            // grows once the app is running again; frames that stop arriving do.
            // It stays disarmed until the session has pushed its first frame,
            // otherwise the initial connection would look like starvation.
            const int64_t lastPush  = frames.getLastPushNs();
            const int64_t silenceNs = lastPush == 0 ? 0 : AVFrameHolder::nowNs() - lastPush;
            const bool starved =
                lastPush != 0 &&
                silenceNs > std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::seconds(3)).count();

            if (!woke && !starved)
                return false;

            recovery           = Recovery::Probing;
            recoveryPhaseStart = now;
            probeBaseFrames    = frames.getPushedTotal();

            brls::Logger::info(
                "StreamingView: {} ({}ms since the last video frame, {}ms since "
                "the last draw), probing the stream",
                woke ? "wake detected" : "stream starved",
                lastPush == 0 ? 0 : silenceNs / 1000000,
                std::chrono::duration_cast<std::chrono::milliseconds>(sinceLastDraw).count());
            return false;
        }
        case Recovery::Probing: {
            // Frames decoded before the sleep are still sitting in the queue and
            // never move this counter, so only fresh decodes count as a recovery.
            // A burst that merely drains the packets queued before the sleep does
            // move it though, so the pushes must also still be recent: a live
            // stream keeps producing them, a drained one goes quiet.
            const bool flowing =
                frames.getPushedTotal() >= probeBaseFrames + 3 &&
                (AVFrameHolder::nowNs() - frames.getLastPushNs()) <
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::milliseconds(500)).count();
            if (flowing) {
                brls::Logger::info("StreamingView: stream resumed on its own");
                recovery = Recovery::Idle;
                return false;
            }

            if (now - recoveryPhaseStart < std::chrono::milliseconds(1500))
                return false; // Keep the last frames on screen while waiting.

            beginRecovery(now);
            return true;
        }
        case Recovery::Recovering: {
            if (frames.getPushedTotal() > recoveryBaseFrames) {
                brls::Logger::info("StreamingView: stream recovered");
                loader->setHidden(true);
                recovery     = Recovery::Idle;
                lastDrawTime = now;
                return false; // The first new frame is drawn on this very frame.
            }

            if (session->is_terminated()) {
                brls::Logger::error("StreamingView: the restarted session was terminated");
                abandonRecovery();
                terminate(false);
                return true;
            }

            if (now - recoveryPhaseStart > std::chrono::seconds(8)) {
                brls::Logger::error("StreamingView: stream recovery timed out");
                abandonRecovery();
                terminate(false);
                return true;
            }

            return true;
        }
    }

    return false;
}

void StreamingView::beginRecovery(std::chrono::steady_clock::time_point now) {
    recovery           = Recovery::Recovering;
    recoveryPhaseStart = now;
    recoveryBaseFrames = AVFrameHolder::instance().getPushedTotal();

    // The spinner goes up before anything is torn down. LiStopConnection() is a
    // synchronous, ordered teardown that can take seconds after a wake, so it
    // runs off the UI thread: this frame and the ones after it still get drawn,
    // which is the difference between "recovering" and "frozen".
    loader->setHidden(false);
    brls::Logger::info("StreamingView: restarting the session after a wake");

    // Capture the session rather than `this`: the worker outlives a dismissal,
    // and ~StreamingView() leaves the session alone while it is restarting.
    MoonlightSession* target = session;
    brls::async([target]() { target->restart(); });
}

void StreamingView::abandonRecovery() {
    loader->setHidden(true);

    // The restart worker may still own the session, so it cannot be deleted from
    // here. Hand it over to nobody: the callbacks become no-ops and the exit
    // path will not block on a teardown that already proved it can wedge.
    MoonlightSession::abandonActiveSession();
}

void StreamingView::terminate(bool terminateApp) {
    if (terminated)
        return;
    terminated = true;

    brls::Logger::info("StreamingView: terminating session (terminate_app={})",
                       terminateApp);

    // A restart in flight already owns the teardown; waiting for it here would
    // block the UI on the same calls that may be wedged.
    if (!session->is_restarting())
        session->stop(terminateApp);

    int controllersCount = Application::getPlatform()->getInputManager()->getControllersConnectedCount();
    for (int i = 0; i < controllersCount; i++)
        Application::getPlatform()->getInputManager()->sendRumble(i, 0, 0);

    bool hasOverlays =
        Application::getActivitiesStack().back() != this->getParentActivity();
    this->dismiss([this, hasOverlays] {
        if (hasOverlays)
            this->dismiss();
    });
}

void StreamingView::handleInput() {
    if (!this->focused) {
        MoonlightInputManager::instance().dropInput();
        return;
    }

    if (keyboard) {
        static KeyboardState oldKeyboardState;
        KeyboardState keyboardState = keyboard->getKeyboardState();

        for (int i = 0; i < _VK_KEY_MAX; i++) {
            if (keyboardState.keys[i] != oldKeyboardState.keys[i]) {
                oldKeyboardState.keys[i] = keyboardState.keys[i];
                LiSendKeyboardEvent(
                    keyboard->getKeyCode((KeyboardKeys)i),
                    keyboardState.keys[i] ? KEY_ACTION_DOWN : KEY_ACTION_UP, 0);
            }
        }

        // Drop input if keyboard overlay presented
//        MoonlightInputManager::instance().dropInput();
    }
//    else {
    sampleGamepadInput();
//    }

#if defined(__SDL2__) || defined(__SDL3__)
    // setBottomBarStatus() is a no-op outside the SDL platforms, so on Switch
    // this whole block was cancelling and re-arming a 3 s timer every frame
    // while a finger was down, to call a function that does nothing.
    if (!Application::currentTouchState.empty()) {
        setBottomBarStatus("2");

        if (bottombarDelayTask != -1)
            cancelDelay(bottombarDelayTask);

        ASYNC_RETAIN
        bottombarDelayTask = delay(3000, [ASYNC_TOKEN]() {
            ASYNC_RELEASE
            setBottomBarStatus("1");
            bottombarDelayTask = -1;
        });
    }
#endif
}

void StreamingView::sampleGamepadInput() {
    if (!this->focused) {
        MoonlightInputManager::instance().dropInput();
        return;
    }

    MoonlightInputManager::instance().handleGamepadOnly(keyboard != nullptr);
}

void StreamingView::handleOverlayCombo() {
    if (!this->focused)
        return;

    const KeyComboOptions& options = Settings::instance().overlay_options();

    // Reuse the snapshot handleInput() took earlier this frame instead of
    // polling the pad again; see MoonlightInputManager::rawControllerState().
    const ControllerState& controller =
        MoonlightInputManager::instance().rawControllerState();

    static std::chrono::high_resolution_clock::time_point clock_counter;
    static bool buttonState = false;
    static bool used = false;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::high_resolution_clock::now() - clock_counter);

    bool buttonsPressed = true;
    for (auto button : options.buttons) {
        buttonsPressed &= controller.buttons[button];
    }

    if (!buttonState && buttonsPressed) {
        buttonState = true;
        clock_counter = std::chrono::high_resolution_clock::now();
    } else if (buttonState && !buttonsPressed) {
        buttonState = false;
        used = false;
    } else if (buttonState && duration.count() >= options.holdTime && !used) {
        used = true;

        auto overlay = new IngameOverlay(this);
        Application::pushActivity(new Activity(overlay));
    }

#ifdef PLATFORM_SWITCH
    static bool oldSystemButtonOverlayPressed = false;
    bool systemButtonOverlayPressed = false;
    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::HOME)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isHomeButtonPressed();

    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::SCREENSHOT)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isScreenshotButtonPressed();

    if (oldSystemButtonOverlayPressed != systemButtonOverlayPressed) {
        oldSystemButtonOverlayPressed = systemButtonOverlayPressed;
        if (systemButtonOverlayPressed) {
            auto overlay = new IngameOverlay(this);
            Application::pushActivity(new Activity(overlay));
        }
    }
#endif
}

void StreamingView::handleMouseInputCombo() {
    if (!this->focused)
        return;

    const KeyComboOptions& options = Settings::instance().mouse_input_options();
    if (options.buttons.empty())
        return;

    const ControllerState& controller =
        MoonlightInputManager::instance().rawControllerState();

    static std::chrono::high_resolution_clock::time_point clock_counter;
    static bool buttonState = false;
    static bool used = false;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::high_resolution_clock::now() - clock_counter);

    bool buttonsPressed = true;
    for (auto button : options.buttons) {
        buttonsPressed &= controller.buttons[button];
    }

    if (!buttonState && buttonsPressed) {
        buttonState = true;
        clock_counter = std::chrono::high_resolution_clock::now();
    } else if (buttonState && !buttonsPressed) {
        buttonState = false;
        used = false;
    } else if (buttonState && duration.count() >= options.holdTime && !used) {
        used = true;

        auto overlay = new StreamingInputOverlay(this);
        Application::pushActivity(new Activity(overlay));
    }
}

void StreamingView::onLayout() {
    Box::onLayout();
    if (loader)
        loader->layout();

    if (keyboardHolder) {
        keyboardHolder->setWidth(getWidth());
        keyboardHolder->setHeight(getHeight());
    }
}

StreamingView::~StreamingView() {
#ifdef PLATFORM_TVOS
    updatePreferredDisplayMode(false);
#endif
    
    Application::getPlatform()->disableScreenDimming(false);
    Application::getPlatform()
        ->getInputManager()
        ->getKeyboardKeyStateChanged()
        ->unsubscribe(keysSubscription);
    Application::getWindowFocusChangedEvent()->unsubscribe(windowFocusSubscription);

    if (session->is_restarting()) {
        // The restart worker still owns the session; deleting it here would be a
        // use-after-free. Leaking it is the policy used everywhere else in this
        // app, and the process is on its way out anyway.
        brls::Logger::warning(
            "StreamingView: dismissed while a restart is still running, "
            "leaking the session");
        MoonlightSession::abandonActiveSession();
        return;
    }

    session->stop(false);
    delete session;
}
