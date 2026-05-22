#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/tab_frame.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

#include "backend.hpp"

// APP_NAME comes from backend's common.h; we add a v2-specific version.
#define NEXPS_VERSION "2.0.0"

// Accent palette
static const NVGcolor ACCENT_MAIN    = nvgRGB(0xB8, 0x47, 0xFF); // base magenta
static const NVGcolor ACCENT_DARK    = nvgRGB(0x88, 0x30, 0xCC);
static const NVGcolor ACCENT_LIGHT   = nvgRGB(0xD0, 0x80, 0xFF);
static const NVGcolor ACCENT_PULSE   = nvgRGBA(0xB8, 0x47, 0xFF, 38);

// ---------- Views ----------

static brls::View* buildPlaceholderTab(const std::string& heading,
                                       const std::string& subtitle) {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setGrow(1.0f);

    auto* head = new brls::Label();
    head->setText(heading);
    head->setFontSize(36.0f);
    head->setTextColor(ACCENT_MAIN);

    auto* sub = new brls::Label();
    sub->setText(subtitle);
    sub->setFontSize(20.0f);
    sub->setTextColor(nvgRGB(0xB0, 0xB0, 0xB8));
    sub->setMarginTop(18.0f);

    box->addView(head);
    box->addView(sub);
    return box;
}

static brls::View* buildAboutTab() {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setGrow(1.0f);

    auto* title = new brls::Label();
    title->setText(APP_NAME " v" NEXPS_VERSION);
    title->setFontSize(48.0f);
    title->setTextColor(ACCENT_MAIN);

    auto* line1 = new brls::Label();
    line1->setText("PSP / PSX game downloader & converter for Switch.");
    line1->setFontSize(20.0f);
    line1->setTextColor(nvgRGB(0xE0, 0xE0, 0xE0));
    line1->setMarginTop(20.0f);

    auto* line2 = new brls::Label();
    line2->setText("Original by joaqmiu — v2 UI rewrite by HayatoG.");
    line2->setFontSize(18.0f);
    line2->setTextColor(nvgRGB(0xA0, 0xA0, 0xA8));
    line2->setMarginTop(12.0f);

    auto* line3 = new brls::Label();
    line3->setText("Built on Borealis + deko3d. Database: NoPayStation + Internet Archive.");
    line3->setFontSize(16.0f);
    line3->setTextColor(nvgRGB(0x80, 0x80, 0x88));
    line3->setMarginTop(24.0f);

    box->addView(title);
    box->addView(line1);
    box->addView(line2);
    box->addView(line3);
    return box;
}

static void registerExitDialog(brls::Activity* activity) {
    activity->registerAction(
        "Exit", brls::ControllerButton::BUTTON_START,
        [](brls::View*) -> bool {
            auto* dialog = new brls::Dialog("Exit NexPS?");
            dialog->addButton("Yes", []() { brls::Application::quit(); });
            dialog->addButton("Cancel", []() {});
            dialog->open();
            return true;
        });
}

static brls::Activity* makeMainActivity() {
    auto* tabFrame = new brls::TabFrame();

    char dbInfo[64];
    snprintf(dbInfo, sizeof(dbInfo), "%d games indexed", total_games);
    std::string dbInfoStr(dbInfo);

    tabFrame->addTab("PSP", [dbInfoStr]() {
        return buildPlaceholderTab("PSP", "List view coming in Phase 2. " + dbInfoStr + ".");
    });
    tabFrame->addTab("PSX", [dbInfoStr]() {
        return buildPlaceholderTab("PSX", "List view coming in Phase 2. " + dbInfoStr + ".");
    });
    tabFrame->addSeparator();
    tabFrame->addTab("+ Add Console", []() {
        return buildPlaceholderTab("Add Console",
            "Internet Archive console flow returns in Phase 5.");
    });
    tabFrame->addSeparator();
    tabFrame->addTab("Settings", []() {
        return buildPlaceholderTab("Settings",
            "Paths, download speed, cheats DB — Phase 3.");
    });
    tabFrame->addTab("About", []() { return buildAboutTab(); });

    auto* appletFrame = new brls::AppletFrame(tabFrame);
    appletFrame->setTitle(APP_NAME " v" NEXPS_VERSION);

    return new brls::Activity(appletFrame);
}

static void applyAccentTheme() {
    auto& dark = brls::Theme::getDarkTheme();
    dark.addColor("brls/accent",                                ACCENT_MAIN);
    dark.addColor("brls/click_pulse",                           ACCENT_PULSE);
    dark.addColor("brls/sidebar/active_item",                   ACCENT_MAIN);
    dark.addColor("brls/highlight/color1",                      ACCENT_DARK);
    dark.addColor("brls/highlight/color2",                      ACCENT_LIGHT);
    dark.addColor("brls/button/primary_enabled_background",     ACCENT_MAIN);
    dark.addColor("brls/button/highlight_enabled_text",         ACCENT_MAIN);
    dark.addColor("brls/button/highlight_disabled_text",        ACCENT_MAIN);
    dark.addColor("brls/list/listItem_value_color",             ACCENT_MAIN);
    dark.addColor("brls/slider/line_filled",                    ACCENT_MAIN);
}

// ---------- Entry point ----------

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        }
    }

    db_buffer = (char*)malloc(DB_BUFFER_SIZE);
    if (!db_buffer) {
        return EXIT_FAILURE;
    }

    if (!brls::Application::init()) {
        brls::Logger::error("Failed to initialise Borealis");
        free(db_buffer);
        return EXIT_FAILURE;
    }

    brls::Application::createWindow(APP_NAME " v" NEXPS_VERSION);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(false);

    applyAccentTheme();

    // Phase 1: UI skeleton without DB load. The NoPayStation fetch will
    // come back in Phase 1.1 wrapped in a libnx Thread (not std::thread —
    // devkitA64's libstdc++ threading was causing a silent crash inside
    // Eden / similar emulators). For now, custom consoles are read from
    // SD and the game count starts at zero; tabs show placeholder copy.
    total_games = 0;
    ia_load_consoles();

    auto* mainActivity = makeMainActivity();
    registerExitDialog(mainActivity);
    brls::Application::pushActivity(mainActivity);

    while (brls::Application::mainLoop()) { }

    free(db_buffer);
    return EXIT_SUCCESS;
}
