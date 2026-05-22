#include <borealis.hpp>
#include <borealis/views/dialog.hpp>
#include <cstdlib>
#include <cstring>

#define APP_NAME    "NexPS"
#define APP_VERSION "2.0.0"
#define ACCENT_R    0xB8
#define ACCENT_G    0x47
#define ACCENT_B    0xFF

static brls::View* buildHelloView() {
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setGrow(1.0f);
    root->setBackgroundColor(nvgRGB(0x18, 0x18, 0x1E));

    auto* title = new brls::Label();
    title->setText(APP_NAME " v" APP_VERSION);
    title->setFontSize(56.0f);
    title->setTextColor(nvgRGB(ACCENT_R, ACCENT_G, ACCENT_B));

    auto* subtitle = new brls::Label();
    subtitle->setText("Borealis foundation OK.");
    subtitle->setFontSize(22.0f);
    subtitle->setTextColor(nvgRGB(0xE0, 0xE0, 0xE0));
    subtitle->setMarginTop(12.0f);

    auto* hint = new brls::Label();
    hint->setText("Press + for exit dialog.");
    hint->setFontSize(16.0f);
    hint->setTextColor(nvgRGB(0x80, 0x80, 0x88));
    hint->setMarginTop(40.0f);

    root->addView(title);
    root->addView(subtitle);
    root->addView(hint);
    return root;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        }
    }

    if (!brls::Application::init()) {
        brls::Logger::error("Failed to initialise Borealis");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow(APP_NAME " v" APP_VERSION);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(false);

    auto* activity = new brls::Activity(buildHelloView());
    brls::Application::pushActivity(activity);

    activity->registerAction(
        "Exit",
        brls::ControllerButton::BUTTON_START,
        [](brls::View*) -> bool {
            auto* dialog = new brls::Dialog("Exit NexPS?");
            dialog->addButton("Yes", []() { brls::Application::quit(); });
            dialog->addButton("Cancel", []() {});
            dialog->open();
            return true;
        });

    while (brls::Application::mainLoop()) { }

    return EXIT_SUCCESS;
}
