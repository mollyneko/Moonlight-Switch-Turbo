//
//  main.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

// Switch include only necessary for demo videos recording
#ifdef __SWITCH__
#include <switch.h>
#endif

#ifdef __PSV__
extern "C" {
// PVR_PSP2 allocates its EGL/GLES state from the Sony libc heap. Match the
// memory model used by working Borealis Vita applications such as wiliwili.
unsigned int _newlib_heap_size_user      = 220 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;
unsigned int sceLibcHeapSize             = 24 * 1024 * 1024;
}
#endif

#include <cstdio>
#include <cstdlib>

#include <borealis.hpp>
#include <string>

#include "add_host_tab.hpp"
#include "host_tab.hpp"
#include "link_cell.hpp"
#include "main_activity.hpp"
#include "main_tabs_view.hpp"
#include "settings_tab.hpp"
#include "views/boolean_slider_cell.hpp"

#include "DiscoverManager.hpp"
#include "FileLogger.hpp"
#include "MoonlightSession.hpp"
#include "SwitchMoonlightSessionDecoderAndRenderProvider.hpp"


#if defined(_WIN32) && defined(__SDL2__)
#include <SDL.h>
#define SDL_MAIN
#endif

#if defined(__SDL3__)
#include <SDL3/SDL_main.h>
#elif defined(__SDL2__)
#include <SDL_main.h>
#endif
#include <main_args.hpp>

using namespace brls::literals; // for _i18n

#ifdef __SWITCH__
namespace {

s32 selectAllowedSwitchCore(u64 affinityMask, int ordinal) {
    s32 lastAllowedCore = -1;

    for (s32 core = 0; core < 4; core++) {
        if ((affinityMask & (1ULL << core)) == 0) {
            continue;
        }

        lastAllowedCore = core;
        if (ordinal == 0) {
            return core;
        }

        ordinal--;
    }

    return lastAllowedCore;
}

void preferSwitchCore(int ordinal) {
    s32 preferredCore = -1;
    u64 affinityMask = 0;
    if (R_FAILED(svcGetThreadCoreMask(&preferredCore, &affinityMask, CUR_THREAD_HANDLE))) {
        return;
    }

    s32 targetCore = selectAllowedSwitchCore(affinityMask, ordinal);
    if (targetCore >= 0 && targetCore != preferredCore) {
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, targetCore, static_cast<u32>(affinityMask));
    }
}

} // namespace
#endif

int main(int argc, char* argv[]) {
    // Enable recording for Twitter memes
#ifdef __SWITCH__
    appletInitializeGamePlayRecording();
    appletSetWirelessPriorityMode(AppletWirelessPriorityMode_OptimizedForWlan);

    // Keep the UI loop away from the hottest streaming worker core.
    preferSwitchCore(0);

    // Keep the main thread above others so that the program stays responsive
    // when doing software decoding
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);

    // auto at = appletGetAppletType();
    // g_application_mode = at == AppletType_Application || at == AppletType_SystemApplication;

    // // To get access to /dev/nvhost-nvjpg, we need nvdrv:{a,s,t}
    // // However, nvdrv:{a,s} have limited address space for gpu mappings
    // extern u32 __nx_nv_service_type, __nx_nv_transfermem_size;
    // __nx_nv_service_type     = NvServiceType_Factory;
    // __nx_nv_transfermem_size = (g_application_mode ? 16 : 3) * 0x100000;
#endif

    // Set log level
    // INFO keeps the on-SD log small: DEBUG would emit per-frame chatter
    // (frame mappings, rumble, renderer FPS) that is useless for diagnosing a
    // slow stream. Everything the telemetry below reports is INFO.
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    registerDeepLinkHandler();

#if defined(PLATFORM_VISIONOS)
    brls::Application::setMaximumUIScale(1.0f);
#endif

    MoonlightSession::set_provider(
            new SwitchMoonlightSessionDecoderAndRenderProvider());

    brls::Application::createWindow("title"_i18n);

    auto home = Application::getPlatform()->getHomeDirectory("Moonlight-Switch");
    Settings::instance().set_working_dir(home);
    Settings::instance().set_launch_path(argc > 0 ? argv[0] : "");
    brls::Logger::info("Working dir, {}", home);

    // Optional on-SD log, toggled from Settings > Debug. The telemetry below is
    // written here, which is the only way to diagnose a bad stream on a retail
    // unit (stdout is never shown there).
    FileLog::setEnabled(Settings::instance().log_to_file());

#ifdef __SWITCH__
    // Running from the album (applet mode) gives the process a smaller memory
    // budget and lower clocks than a full application, which by itself can
    // explain stutter that never appears on the host. Record it up front.
    brls::Logger::info(
        "Switch: applet_type={} operation_mode={} window={}x{}",
        (int) appletGetAppletType(), (int) appletGetOperationMode(),
        brls::Application::windowWidth, brls::Application::windowHeight);
#endif

    // Have the application register an action on every activity that will quit
    // when you press BUTTON_START
    brls::Application::setGlobalQuit(false);
    brls::Application::setFPSStatus(false);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("BooleanSliderCell", BooleanSliderCell::create);
    brls::Application::registerXMLView("LinkCell", LinkCell::create);

    brls::Application::registerXMLView("MainTabs", MainTabs::create);
    brls::Application::registerXMLView("HostTab", HostTab::create);
    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);

    // Add custom values to the theme
    brls::Theme::getLightTheme().addColor("captioned_image/caption",
                                   nvgRGB(2, 176, 183));
    brls::Theme::getDarkTheme().addColor("captioned_image/caption",
                                  nvgRGB(51, 186, 227));

    // Add custom values to the style
    brls::getStyle().addMetric("about/padding_top_bottom", 50);
    brls::getStyle().addMetric("about/padding_sides", 75);
    brls::getStyle().addMetric("about/description_margin", 50);

    // Create and push the main activity to the stack if cannot run game from arguments
    if (!startFromArgs(argc, argv)) {
        brls::Application::pushActivity(new MainActivity());
    }

    brls::Application::enableDebuggingView(Settings::instance().write_log());
    brls::Application::setSwapInputKeys(Settings::instance().swap_ui_keys());

    // Run the app. The Vita development loop waits for this marker so a
    // successful launch means at least one complete Borealis frame rendered.
#ifdef __PSV__
    bool vitaHealthReported = false;
#endif
    while (brls::Application::mainLoop()) {
#ifdef __PSV__
        if (!vitaHealthReported) {
            brls::Logger::info("VITA_HEALTH: READY");
            vitaHealthReported = true;
        }
#endif
    }

    // Exit
#ifdef __SWITCH__
    // HOME -> Close ends this process through an applet exit request, which
    // never runs StreamingView::terminate(): the streaming sockets, NVDEC, the
    // audio output and the deko3d GPU channel were all still live when the
    // kernel tore the process down. A healthy console survives that, but after
    // a sleep/wake cycle those sessions are stale and the teardown blocks in
    // nvhost/audout, which freezes the whole console (HOME stops responding
    // too). Stop the session here instead, while we can still order it.
    if (auto* session = MoonlightSession::activeSession()) {
        session->stop(0);
        // Runs ~MoonlightSession -> ~DKVideoRenderer, which waits for the GPU
        // to go idle before its resources go away. The session is leaked
        // everywhere else, so this is the only place that ever happens.
        delete session;
    }
    // NOTE: libnx provides no "end" counterpart to appletInitializeGamePlayRecording()
    // (only appletIsGamePlayRecordingSupported / appletSetGamePlayRecordingState /
    // appletInitializeGamePlayRecording). Its recording resources are released
    // automatically by appletExit() during process teardown, so no explicit
    // teardown call is required or available here.
#endif

#if defined(PLATFORM_TVOS)
    exit(0);
#endif
    
    return EXIT_SUCCESS;
}
