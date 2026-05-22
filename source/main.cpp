#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/recycler.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/tab_frame.hpp>

#include <vector>

#include <atomic>
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

// ---------- Async DB load (libnx Thread API, not std::thread) ----------

enum class DbState { Loading, Ready, Error };

static std::atomic<DbState> g_dbState{DbState::Loading};
static std::atomic<int>     g_dbStep{0}; // 0=init, 1=PSP fetch, 2=PSX fetch, 3=parsing
static Thread               g_dbThread;
static bool                 g_dbThreadStarted = false;

// libnx Thread entry — must be plain C function pointer signature.
static void dbLoaderEntry(void*) {
    ia_load_consoles();
    total_games = 0;

    MemoryStruct chunkPSP = { (char*)malloc(1), 0 };
    MemoryStruct chunkPSX = { (char*)malloc(1), 0 };

    g_dbStep.store(1);
    fetch_to_memory(URL_NPS_PSP, &chunkPSP);

    g_dbStep.store(2);
    fetch_to_memory(URL_NPS_PSX, &chunkPSX);

    g_dbStep.store(3);
    size_t total_size = chunkPSP.size + chunkPSX.size;
    if (total_size > 0 && total_size < DB_BUFFER_SIZE - 2) {
        size_t offset = 0;
        if (chunkPSP.size > 0) {
            memcpy(db_buffer, chunkPSP.memory, chunkPSP.size);
            db_buffer[chunkPSP.size] = '\0';
            parse_db(db_buffer, "PSP");
            offset += chunkPSP.size + 1;
        }
        if (chunkPSX.size > 0) {
            memcpy(db_buffer + offset, chunkPSX.memory, chunkPSX.size);
            db_buffer[offset + chunkPSX.size] = '\0';
            parse_db(db_buffer + offset, "PSX");
        }
        g_dbState.store(DbState::Ready);
    } else {
        g_dbState.store(DbState::Error);
    }

    free(chunkPSP.memory);
    free(chunkPSX.memory);
}

static void startDbLoad() {
    constexpr size_t STACK_SZ = 0x20000; // 128 KB — curl + parsing room
    Result r = threadCreate(&g_dbThread, dbLoaderEntry, nullptr,
                            nullptr, STACK_SZ, 0x2C, -2);
    if (R_FAILED(r)) {
        brls::Logger::error("threadCreate failed: 0x{:x}", r);
        g_dbState.store(DbState::Error);
        return;
    }
    r = threadStart(&g_dbThread);
    if (R_FAILED(r)) {
        brls::Logger::error("threadStart failed: 0x{:x}", r);
        threadClose(&g_dbThread);
        g_dbState.store(DbState::Error);
        return;
    }
    g_dbThreadStarted = true;
}

static void joinDbThread() {
    if (!g_dbThreadStarted) return;
    threadWaitForExit(&g_dbThread);
    threadClose(&g_dbThread);
    g_dbThreadStarted = false;
}

// ---------- Views ----------

static brls::View* buildSplashView(brls::Label** outStatus) {
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setGrow(1.0f);

    auto* title = new brls::Label();
    title->setText(APP_NAME " v" NEXPS_VERSION);
    title->setFontSize(56.0f);
    title->setTextColor(ACCENT_MAIN);

    auto* status = new brls::Label();
    status->setText("Initializing...");
    status->setFontSize(22.0f);
    status->setTextColor(nvgRGB(0xE0, 0xE0, 0xE0));
    status->setMarginTop(28.0f);

    root->addView(title);
    root->addView(status);

    *outStatus = status;
    return root;
}

// ---------- Game list ----------

class GameCell : public brls::RecyclerCell {
public:
    GameCell() {
        this->setAxis(brls::Axis::ROW);
        this->setPaddingTop(14.0f);
        this->setPaddingBottom(14.0f);
        this->setPaddingLeft(24.0f);
        this->setPaddingRight(24.0f);
        this->setFocusable(true);
        this->setHeight(56.0f);

        platformBadge = new brls::Label();
        platformBadge->setFontSize(15.0f);
        platformBadge->setTextColor(ACCENT_MAIN);
        platformBadge->setWidth(48.0f);

        title = new brls::Label();
        title->setFontSize(20.0f);
        title->setGrow(1.0f);
        title->setMarginLeft(12.0f);
        title->setMarginRight(12.0f);

        region = new brls::Label();
        region->setFontSize(15.0f);
        region->setTextColor(nvgRGB(0x90, 0x90, 0x98));
        region->setWidth(80.0f);

        this->addView(platformBadge);
        this->addView(title);
        this->addView(region);
    }

    void setGame(const GameEntry* g) {
        platformBadge->setText(g ? g->platform : "");
        title->setText(g ? g->name : "");
        region->setText(g ? g->region : "");
    }

private:
    brls::Label* platformBadge = nullptr;
    brls::Label* title         = nullptr;
    brls::Label* region        = nullptr;
};

class GamesDataSource : public brls::RecyclerDataSource {
public:
    explicit GamesDataSource(const char* platformFilter) {
        all.reserve(total_games / 2);
        for (int i = 0; i < total_games; i++) {
            if (stristr(all_games[i].platform, platformFilter) != nullptr) {
                all.push_back(&all_games[i]);
            }
        }
        applyFilter();
    }

    int  totalForPlatform() const { return (int)all.size(); }
    void setSearchQuery(const std::string& q) {
        query = q;
        applyFilter();
    }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return (int)visible.size();
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath idx) override {
        auto* cell = (GameCell*)recycler->dequeueReusableCell("game");
        cell->setGame(visible[idx.row]);
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath idx) override {
        const GameEntry* g = visible[idx.row];
        char body[1024];
        snprintf(body, sizeof(body),
                 "%s\n\nPlatform: %s\nRegion: %s\nID: %s\n\n"
                 "Download flow returns in Phase 6.",
                 g->name, g->platform, g->region, g->id);
        auto* dialog = new brls::Dialog(std::string(body));
        dialog->addButton("OK", []() {});
        dialog->open();
    }

private:
    void applyFilter() {
        visible.clear();
        if (query.empty()) {
            visible = all;
            return;
        }
        visible.reserve(all.size());
        for (auto* g : all) {
            if (stristr(g->name, query.c_str()) != nullptr) {
                visible.push_back(g);
            }
        }
    }

    std::vector<GameEntry*> all;
    std::vector<GameEntry*> visible;
    std::string             query;
};

static brls::View* buildGameListTab(const char* platformFilter) {
    auto* ds = new GamesDataSource(platformFilter);

    if (ds->totalForPlatform() == 0) {
        delete ds;
        auto* box = new brls::Box(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setGrow(1.0f);

        auto* msg = new brls::Label();
        msg->setText("No games loaded.");
        msg->setFontSize(28.0f);
        msg->setTextColor(ACCENT_MAIN);

        auto* hint = new brls::Label();
        hint->setText("Database fetch did not complete. Re-open with internet.");
        hint->setFontSize(18.0f);
        hint->setTextColor(nvgRGB(0xB0, 0xB0, 0xB8));
        hint->setMarginTop(16.0f);

        box->addView(msg);
        box->addView(hint);
        return box;
    }

    auto* recycler = new brls::RecyclerFrame();
    recycler->registerCell("game", []() { return new GameCell(); });
    recycler->estimatedRowHeight = 56.0f;
    recycler->setDataSource(ds);

    recycler->registerAction(
        "Search", brls::ControllerButton::BUTTON_Y,
        [recycler, ds](brls::View*) -> bool {
            brls::Application::getImeManager()->openForText(
                [recycler, ds](std::string text) {
                    ds->setSearchQuery(text);
                    recycler->reloadData();
                },
                "Search games", "", 64, "");
            return true;
        });

    return recycler;
}

// ---------- Settings ----------

static brls::View* buildSettingsTab() {
    auto* scroll    = new brls::ScrollingFrame();
    auto* container = new brls::Box(brls::Axis::COLUMN);
    container->setPadding(24.0f);

    auto* pspCell = new brls::InputCell();
    pspCell->init(
        "PSP install path", install_path_psp,
        [](std::string value) {
            strncpy(install_path_psp, value.c_str(), sizeof(install_path_psp) - 1);
            install_path_psp[sizeof(install_path_psp) - 1] = '\0';
            save_config();
        },
        DEFAULT_INSTALL_PATH_PSP, "Where extracted PSP games are written", 256, 0);

    auto* psxCell = new brls::InputCell();
    psxCell->init(
        "PSX install path", install_path_psx,
        [](std::string value) {
            strncpy(install_path_psx, value.c_str(), sizeof(install_path_psx) - 1);
            install_path_psx[sizeof(install_path_psx) - 1] = '\0';
            save_config();
        },
        DEFAULT_INSTALL_PATH_PSX, "Where extracted PSX BIN/CUE pairs land", 256, 0);

    auto* speedCell = new brls::SelectorCell();
    int speedIdx = (selected_threads == 1) ? 0 : (selected_threads == 8 ? 2 : 1);
    speedCell->init(
        "Download speed",
        { "Slow / Stable (1 thread)",
          "Good / Recommended (4 threads)",
          "Fast / Unstable (8 threads)" },
        speedIdx,
        [](int idx) {
            selected_threads = (idx == 0) ? 1 : (idx == 2 ? 8 : 4);
            save_config();
        });

    auto* cheatsCell = new brls::DetailCell();
    cheatsCell->setText("Download Cheats DB");
    cheatsCell->setDetailText("CWCheat Plus (~5 MB) — Phase 6");
    cheatsCell->setFocusable(true);
    cheatsCell->registerClickAction([](brls::View*) -> bool {
        auto* dialog = new brls::Dialog(
            "The Cheats DB download will return in Phase 6, sharing the\n"
            "same threaded downloader that the PSP / PSX titles will use.");
        dialog->addButton("OK", []() {});
        dialog->open();
        return true;
    });

    container->addView(pspCell);
    container->addView(psxCell);
    container->addView(speedCell);
    container->addView(cheatsCell);

    scroll->setContentView(container);
    return scroll;
}

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

    tabFrame->addTab("PSP", []() { return buildGameListTab("PSP"); });
    tabFrame->addTab("PSX", []() { return buildGameListTab("PSX"); });
    tabFrame->addSeparator();
    tabFrame->addTab("+ Add Console", []() {
        return buildPlaceholderTab("Add Console",
            "Internet Archive console flow returns in Phase 5.");
    });
    tabFrame->addSeparator();
    tabFrame->addTab("Settings", []() { return buildSettingsTab(); });
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

    // Phase 1.1: splash + libnx-Thread DB load + runloop-driven swap.
    // ia_load_consoles is called inside the worker (file I/O + fetch).
    brls::Label* statusLabel = nullptr;
    auto* splashActivity = new brls::Activity(buildSplashView(&statusLabel));
    brls::Application::pushActivity(splashActivity);
    registerExitDialog(splashActivity);

    startDbLoad();

    static bool swapped = false;
    brls::Application::getRunLoopEvent()->subscribe([statusLabel]() {
        if (swapped) return;

        DbState state = g_dbState.load();
        int     step  = g_dbStep.load();

        if (state == DbState::Ready) {
            auto* mainActivity = makeMainActivity();
            registerExitDialog(mainActivity);
            brls::Application::popActivity(brls::TransitionAnimation::FADE);
            brls::Application::pushActivity(mainActivity);
            swapped = true;
            return;
        }
        if (state == DbState::Error) {
            statusLabel->setText("Database fetch failed. Press + and reopen.");
            swapped = true;
            return;
        }
        switch (step) {
            case 1: statusLabel->setText("Downloading PSP database..."); break;
            case 2: statusLabel->setText("Downloading PSX database..."); break;
            case 3: statusLabel->setText("Parsing games..."); break;
            default: statusLabel->setText("Initializing..."); break;
        }
    });

    while (brls::Application::mainLoop()) { }

    // Intentionally not calling joinDbThread() — if the loader is still in
    // a blocking curl call (e.g. an unfinished SSL handshake against a
    // stubbed emulator service), threadWaitForExit would hang the exit.
    // The kernel reaps the thread on process termination.
    free(db_buffer);
    return EXIT_SUCCESS;
}
