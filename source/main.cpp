#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/recycler.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/tab_frame.hpp>

#include <algorithm>
#include <vector>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/stat.h>

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

// Count of NoPayStation entries parsed. Custom-console (Internet Archive)
// entries are appended to all_games[] each time the user opens a console;
// before each append we reset total_games back to this snapshot so old IA
// data doesn't accumulate.
static int g_nps_total_games = 0;

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
        g_nps_total_games = total_games;
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

// ---------- Phase 6: async download + extract ----------

static void mkdir_p(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

enum class DlPhase {
    Idle,
    Downloading,
    DecryptPkg,
    UnpackBin,
    Archive,
    Done,
    Error,
    Cancelled
};

struct DownloadState {
    std::atomic<DlPhase>   phase{DlPhase::Idle};
    std::atomic<long long> bytesCurrent{0};
    std::atomic<long long> bytesTotal{0};
    std::atomic<int>       percent{0};
    std::atomic<bool>      cancelRequested{false};

    char finalPath[1024]    = {0};
    char errorMessage[256]  = {0};
    char gameTitle[256]     = {0};
};

static DownloadState g_dl;

struct DownloadJob {
    char url[1024];
    char tempPath[1024];
    char targetDir[512];
    char safeName[256];
    int  isPsx;
    int  isArchive;
    int  threads;
    int  noExtract;     // 1 for cheats DB and similar raw files
};
static DownloadJob g_job;

static Thread g_dlThread;
static bool   g_dlThreadStarted = false;

static void dlNetProgress(long long downloaded, long long total, void*) {
    g_dl.bytesCurrent.store(downloaded);
    g_dl.bytesTotal.store(total);
    int pct = (total > 0) ? (int)((downloaded * 100LL) / total) : 0;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    g_dl.percent.store(pct);
}

static int dlNetCancel(void*) {
    return g_dl.cancelRequested.load() ? 1 : 0;
}

static void dlConvProgress(int phase, long long current, long long total, void*) {
    DlPhase p = DlPhase::DecryptPkg;
    if (phase == CONV_PHASE_PSAR)    p = DlPhase::UnpackBin;
    if (phase == CONV_PHASE_ARCHIVE) p = DlPhase::Archive;
    g_dl.phase.store(p);
    g_dl.bytesCurrent.store(current);
    g_dl.bytesTotal.store(total);
    if (total > 0) {
        int pct = (int)((current * 100LL) / total);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        g_dl.percent.store(pct);
    } else {
        g_dl.percent.store(0);
    }
}

static void dlWorkerEntry(void*) {
    mkdir_p(g_job.targetDir);

    g_dl.phase.store(DlPhase::Downloading);
    g_dl.percent.store(0);
    g_dl.bytesCurrent.store(0);
    g_dl.bytesTotal.store(0);

    int threads = g_job.threads;
    if (threads < 1) threads = 1;
    if (threads > 8) threads = 8;

    int result = download_file(g_job.url, g_job.tempPath, threads,
                               dlNetProgress, dlNetCancel, nullptr);

    if (result == -1) {
        remove(g_job.tempPath);
        g_dl.phase.store(DlPhase::Cancelled);
        return;
    }
    if (result != 1) {
        remove(g_job.tempPath);
        snprintf(g_dl.errorMessage, sizeof(g_dl.errorMessage),
                 "Download failed. Check internet connection.");
        g_dl.phase.store(DlPhase::Error);
        return;
    }

    if (g_job.noExtract) {
        snprintf(g_dl.finalPath, sizeof(g_dl.finalPath), "%s", g_job.tempPath);
        g_dl.phase.store(DlPhase::Done);
        return;
    }

    if (g_job.isArchive) {
        g_dl.phase.store(DlPhase::Archive);
        g_dl.percent.store(0);
        g_dl.bytesCurrent.store(0);
        g_dl.bytesTotal.store(-1);
        if (!extract_archive(g_job.tempPath, g_job.targetDir, dlConvProgress, nullptr)) {
            snprintf(g_dl.errorMessage, sizeof(g_dl.errorMessage),
                     "Archive extraction failed.");
            g_dl.phase.store(DlPhase::Error);
            return;
        }
        remove(g_job.tempPath);
        snprintf(g_dl.finalPath, sizeof(g_dl.finalPath), "%s", g_job.targetDir);
        g_dl.phase.store(DlPhase::Done);
        return;
    }

    g_dl.phase.store(DlPhase::DecryptPkg);
    g_dl.percent.store(0);
    g_dl.bytesCurrent.store(0);
    g_dl.bytesTotal.store(0);

    if (!extract_game_from_pkg(g_job.tempPath, g_job.targetDir,
                               g_job.safeName, g_job.isPsx,
                               dlConvProgress, nullptr)) {
        remove(g_job.tempPath);
        snprintf(g_dl.errorMessage, sizeof(g_dl.errorMessage),
                 "PKG extraction failed.");
        g_dl.phase.store(DlPhase::Error);
        return;
    }

    remove(g_job.tempPath);
    snprintf(g_dl.finalPath, sizeof(g_dl.finalPath), "%s", g_job.targetDir);
    g_dl.phase.store(DlPhase::Done);
}

static bool startDownloadJob() {
    if (g_dlThreadStarted) {
        threadWaitForExit(&g_dlThread);
        threadClose(&g_dlThread);
        g_dlThreadStarted = false;
    }

    g_dl.cancelRequested.store(false);
    g_dl.phase.store(DlPhase::Downloading);
    g_dl.percent.store(0);
    g_dl.bytesCurrent.store(0);
    g_dl.bytesTotal.store(0);
    g_dl.errorMessage[0] = '\0';
    g_dl.finalPath[0]    = '\0';

    constexpr size_t STACK_SZ = 0x40000; // 256 KB — curl + AES-CTR + libarchive
    Result r = threadCreate(&g_dlThread, dlWorkerEntry, nullptr,
                            nullptr, STACK_SZ, 0x2C, -2);
    if (R_FAILED(r)) {
        brls::Logger::error("download threadCreate failed: 0x{:x}", r);
        g_dl.phase.store(DlPhase::Error);
        snprintf(g_dl.errorMessage, sizeof(g_dl.errorMessage),
                 "Could not start worker thread.");
        return false;
    }
    r = threadStart(&g_dlThread);
    if (R_FAILED(r)) {
        brls::Logger::error("download threadStart failed: 0x{:x}", r);
        threadClose(&g_dlThread);
        g_dl.phase.store(DlPhase::Error);
        snprintf(g_dl.errorMessage, sizeof(g_dl.errorMessage),
                 "Could not start worker thread.");
        return false;
    }
    g_dlThreadStarted = true;
    return true;
}

class DownloadView; // fwd
static DownloadView* g_dlView = nullptr;

static bool dlPhaseIsTerminal(DlPhase p) {
    return p == DlPhase::Done || p == DlPhase::Error || p == DlPhase::Cancelled;
}

class DownloadView : public brls::Box {
public:
    DownloadView(const std::string& gameName) {
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setGrow(1.0f);
        this->setPadding(40.0f);

        title = new brls::Label();
        title->setText(gameName);
        title->setFontSize(28.0f);
        title->setTextColor(ACCENT_MAIN);
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);

        phaseLabel = new brls::Label();
        phaseLabel->setText("Preparing...");
        phaseLabel->setFontSize(22.0f);
        phaseLabel->setTextColor(nvgRGB(0xE0, 0xE0, 0xE0));
        phaseLabel->setMarginTop(28.0f);
        phaseLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);

        percentLabel = new brls::Label();
        percentLabel->setText("0%");
        percentLabel->setFontSize(72.0f);
        percentLabel->setTextColor(ACCENT_LIGHT);
        percentLabel->setMarginTop(16.0f);
        percentLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);

        bytesLabel = new brls::Label();
        bytesLabel->setText("");
        bytesLabel->setFontSize(18.0f);
        bytesLabel->setTextColor(nvgRGB(0xB0, 0xB0, 0xB8));
        bytesLabel->setMarginTop(8.0f);
        bytesLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);

        actionButton = new brls::Button();
        actionButton->setStyle(&brls::BUTTONSTYLE_BORDERED);
        actionButton->setText("Cancel");
        actionButton->setMarginTop(40.0f);
        actionButton->setWidth(220.0f);
        actionButton->setHeight(56.0f);
        actionButton->registerClickAction([](brls::View*) -> bool {
            DlPhase p = g_dl.phase.load();
            if (dlPhaseIsTerminal(p)) {
                brls::Application::popActivity(brls::TransitionAnimation::FADE);
                return true;
            }
            auto* dialog = new brls::Dialog("Cancel download?");
            dialog->addButton("Yes", []() {
                g_dl.cancelRequested.store(true);
            });
            dialog->addButton("No", []() {});
            dialog->open();
            return true;
        });

        this->addView(title);
        this->addView(phaseLabel);
        this->addView(percentLabel);
        this->addView(bytesLabel);
        this->addView(actionButton);

        g_dlView = this;
    }

    ~DownloadView() override {
        if (g_dlView == this) g_dlView = nullptr;
    }

    void tick() {
        DlPhase ph = g_dl.phase.load();
        switch (ph) {
            case DlPhase::Downloading:
                phaseLabel->setText("Downloading");
                break;
            case DlPhase::DecryptPkg:
                phaseLabel->setText("Decrypting PKG");
                break;
            case DlPhase::UnpackBin:
                phaseLabel->setText("Unpacking BIN");
                break;
            case DlPhase::Archive:
                phaseLabel->setText("Extracting archive");
                break;
            case DlPhase::Done:
                phaseLabel->setText("Complete");
                percentLabel->setText("100%");
                bytesLabel->setText(std::string("Saved to: ") + g_dl.finalPath);
                actionButton->setText("Back");
                return;
            case DlPhase::Error:
                phaseLabel->setText("Error");
                percentLabel->setText("--");
                bytesLabel->setText(g_dl.errorMessage);
                actionButton->setText("Back");
                return;
            case DlPhase::Cancelled:
                phaseLabel->setText("Cancelled");
                percentLabel->setText("--");
                bytesLabel->setText("");
                actionButton->setText("Back");
                return;
            default:
                phaseLabel->setText("Preparing...");
                break;
        }

        int pct = g_dl.percent.load();
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
        percentLabel->setText(pbuf);

        long long cur   = g_dl.bytesCurrent.load();
        long long total = g_dl.bytesTotal.load();
        char bbuf[128];
        if (total > 0) {
            snprintf(bbuf, sizeof(bbuf), "%.2f / %.2f MB",
                     (double)cur / (1024.0 * 1024.0),
                     (double)total / (1024.0 * 1024.0));
        } else if (cur > 0) {
            snprintf(bbuf, sizeof(bbuf), "%.2f MB",
                     (double)cur / (1024.0 * 1024.0));
        } else {
            bbuf[0] = '\0';
        }
        bytesLabel->setText(bbuf);
    }

private:
    brls::Label*  title         = nullptr;
    brls::Label*  phaseLabel    = nullptr;
    brls::Label*  percentLabel  = nullptr;
    brls::Label*  bytesLabel    = nullptr;
    brls::Button* actionButton  = nullptr;
};

static brls::Activity* makeDownloadActivity(const std::string& gameName) {
    auto* view     = new DownloadView(gameName);
    auto* activity = new brls::Activity(view);
    return activity;
}

// Activity::registerAction delegates to contentView, which is only populated
// during pushActivity. Call this AFTER pushing or the registration is a no-op.
static void registerDownloadBackAction(brls::Activity* activity) {
    activity->registerAction(
        "Cancel", brls::ControllerButton::BUTTON_B,
        [](brls::View*) -> bool {
            DlPhase p = g_dl.phase.load();
            if (dlPhaseIsTerminal(p)) {
                brls::Application::popActivity(brls::TransitionAnimation::FADE);
                return true;
            }
            auto* dialog = new brls::Dialog("Cancel download?");
            dialog->addButton("Yes", []() {
                g_dl.cancelRequested.store(true);
            });
            dialog->addButton("No", []() {});
            dialog->open();
            return true;
        });
}

static void launchGameDownload(const GameEntry* g) {
    snprintf(g_dl.gameTitle, sizeof(g_dl.gameTitle), "%s", g->name);

    bool is_ia = (g->id != nullptr && strcmp(g->id, "IA") == 0);

    if (is_ia) {
        // Find the custom console whose name matches this game's platform.
        const char* target_dir = nullptr;
        for (int i = 0; i < custom_console_count; i++) {
            if (strcmp(custom_consoles[i].name, g->platform) == 0) {
                target_dir = custom_consoles[i].path;
                break;
            }
        }
        if (!target_dir) {
            auto* dialog = new brls::Dialog(
                std::string("Console install path not found for ") + g->platform + ".");
            dialog->addButton("OK", []() {});
            dialog->open();
            return;
        }
        snprintf(g_job.targetDir, sizeof(g_job.targetDir), "%s", target_dir);
        snprintf(g_job.url, sizeof(g_job.url), "%s", g->url);
        // IA filenames carry their original extension; preserve it.
        snprintf(g_job.tempPath, sizeof(g_job.tempPath),
                 "%s/%s", target_dir, g->name);
        g_job.safeName[0] = '\0';
        g_job.isPsx     = 0;
        g_job.isArchive = (stristr(g->name, ".zip") != nullptr ||
                           stristr(g->name, ".7z")  != nullptr ||
                           stristr(g->name, ".rar") != nullptr);
        g_job.noExtract = g_job.isArchive ? 0 : 1;
        g_job.threads   = 1;
    } else {
        int is_psx = (stristr(g->platform, "PSX") != nullptr);
        const char* target_dir = is_psx ? install_path_psx : install_path_psp;
        snprintf(g_job.targetDir, sizeof(g_job.targetDir), "%s", target_dir);

        snprintf(g_job.safeName, sizeof(g_job.safeName), "%s", g->name);
        sanitize_filename(g_job.safeName);

        snprintf(g_job.url, sizeof(g_job.url), "%s", g->url);
        snprintf(g_job.tempPath, sizeof(g_job.tempPath),
                 "%s/%s_temp.pkg", target_dir, g_job.safeName);

        g_job.isPsx     = is_psx;
        g_job.isArchive = 0;
        g_job.threads   = selected_threads;
        g_job.noExtract = 0;
    }

    auto* activity = makeDownloadActivity(g->name);
    brls::Application::pushActivity(activity);
    registerDownloadBackAction(activity);

    if (!startDownloadJob()) {
        // The view will pick up the Error state on the next tick.
    }
}

static void launchCheatsDownload() {
    snprintf(g_dl.gameTitle, sizeof(g_dl.gameTitle), "CWCheat Database");

    mkdir_p("/switch/ppsspp/config/ppsspp/PSP/Cheats");

    snprintf(g_job.url, sizeof(g_job.url), "%s", URL_CHEATS);
    snprintf(g_job.tempPath, sizeof(g_job.tempPath),
             "/switch/ppsspp/config/ppsspp/PSP/Cheats/cheat.db");
    snprintf(g_job.targetDir, sizeof(g_job.targetDir),
             "/switch/ppsspp/config/ppsspp/PSP/Cheats");
    g_job.safeName[0] = '\0';
    g_job.isPsx     = 0;
    g_job.isArchive = 0;
    g_job.threads   = 1;
    g_job.noExtract = 1;

    auto* activity = makeDownloadActivity("CWCheat Database");
    brls::Application::pushActivity(activity);
    registerDownloadBackAction(activity);

    startDownloadJob();
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

enum class SortMode { AZ, ZA };

class GamesDataSource : public brls::RecyclerDataSource {
public:
    explicit GamesDataSource(const char* platformFilter) {
        all.reserve(total_games / 2);
        for (int i = 0; i < total_games; i++) {
            if (stristr(all_games[i].platform, platformFilter) != nullptr) {
                all.push_back(&all_games[i]);
            }
        }
        sortAll();
        applyFilter();
    }

    int  totalForPlatform() const { return (int)all.size(); }
    void setSearchQuery(const std::string& q) {
        query = q;
        applyFilter();
    }

    void toggleSort() {
        sort = (sort == SortMode::AZ) ? SortMode::ZA : SortMode::AZ;
        sortAll();
        applyFilter();
    }

    SortMode sortMode() const { return sort; }

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
                 "%s\n\nPlatform: %s\nRegion: %s\n\nDownload now?",
                 g->name, g->platform, g->region);
        auto* dialog = new brls::Dialog(std::string(body));
        dialog->addButton("Download", [g]() {
            launchGameDownload(g);
        });
        dialog->addButton("Cancel", []() {});
        dialog->open();
    }

private:
    void sortAll() {
        if (sort == SortMode::AZ) {
            std::sort(all.begin(), all.end(),
                      [](const GameEntry* a, const GameEntry* b) {
                          return strcasecmp(a->name, b->name) < 0;
                      });
        } else {
            std::sort(all.begin(), all.end(),
                      [](const GameEntry* a, const GameEntry* b) {
                          return strcasecmp(a->name, b->name) > 0;
                      });
        }
    }

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
    SortMode                sort = SortMode::AZ;
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

    auto sortActionId = recycler->registerAction(
        "Sort Z-A", brls::ControllerButton::BUTTON_RSB,
        [recycler, ds](brls::View*) -> bool {
            ds->toggleSort();
            recycler->reloadData();
            const char* hint = (ds->sortMode() == SortMode::AZ) ? "Sort Z-A" : "Sort A-Z";
            recycler->updateActionHint(brls::ControllerButton::BUTTON_RSB, hint);
            brls::Application::getGlobalHintsUpdateEvent()->fire();
            return true;
        });
    (void)sortActionId;

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
    cheatsCell->setDetailText("CWCheat Plus (~5 MB)");
    cheatsCell->setFocusable(true);
    cheatsCell->registerClickAction([](brls::View*) -> bool {
        auto* dialog = new brls::Dialog(
            "Download CWCheat Database to\n"
            "/switch/ppsspp/config/ppsspp/PSP/Cheats/cheat.db ?");
        dialog->addButton("Download", []() {
            launchCheatsDownload();
        });
        dialog->addButton("Cancel", []() {});
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

// ---------- Phase 5: custom consoles via Internet Archive ----------

enum class IaState { Idle, Loading, Ready, Error };

static std::atomic<IaState> g_iaState{IaState::Idle};
static Thread               g_iaThread;
static bool                 g_iaThreadStarted = false;
static char                 g_iaTargetConsole[64] = {0};
static char                 g_iaTargetId[128]     = {0};

static void iaLoaderEntry(void*) {
    total_games = g_nps_total_games;
    ia_clear_allocations();

    MemoryStruct chunk = { (char*)malloc(1), 0 };
    char url[512];
    snprintf(url, sizeof(url), "https://archive.org/metadata/%s/files", g_iaTargetId);
    int ok = fetch_to_memory(url, &chunk);

    if (ok && chunk.size > 0) {
        ia_parse_metadata(chunk.memory, g_iaTargetConsole, g_iaTargetId);
        g_iaState.store(IaState::Ready);
    } else {
        g_iaState.store(IaState::Error);
    }

    free(chunk.memory);
}

static bool startIaLoad(const char* consoleName, const char* iaId) {
    if (g_iaThreadStarted) {
        threadWaitForExit(&g_iaThread);
        threadClose(&g_iaThread);
        g_iaThreadStarted = false;
    }
    snprintf(g_iaTargetConsole, sizeof(g_iaTargetConsole), "%s", consoleName);
    snprintf(g_iaTargetId,      sizeof(g_iaTargetId),      "%s", iaId);
    g_iaState.store(IaState::Loading);

    constexpr size_t STACK_SZ = 0x40000;
    Result r = threadCreate(&g_iaThread, iaLoaderEntry, nullptr,
                            nullptr, STACK_SZ, 0x2C, -2);
    if (R_FAILED(r)) {
        g_iaState.store(IaState::Error);
        return false;
    }
    r = threadStart(&g_iaThread);
    if (R_FAILED(r)) {
        threadClose(&g_iaThread);
        g_iaState.store(IaState::Error);
        return false;
    }
    g_iaThreadStarted = true;
    return true;
}

// Token incremented every time we open a new IA loader. The runloop callback
// captures the token it was registered for; once it observes a terminal state
// (or the loading view is destroyed) the global token is bumped, so the
// callback short-circuits on its next fire.
static std::atomic<long> g_iaCallbackToken{0};

class IaLoadingView : public brls::Box {
public:
    IaLoadingView(const std::string& consoleName, long token) : myToken(token) {
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setGrow(1.0f);

        auto* title = new brls::Label();
        title->setText("Loading " + consoleName);
        title->setFontSize(32.0f);
        title->setTextColor(ACCENT_MAIN);

        status = new brls::Label();
        status->setText("Fetching metadata from Internet Archive...");
        status->setFontSize(20.0f);
        status->setTextColor(nvgRGB(0xE0, 0xE0, 0xE0));
        status->setMarginTop(24.0f);

        this->addView(title);
        this->addView(status);
    }

    ~IaLoadingView() override {
        // Invalidate the pending runloop callback so it never touches `status`
        // (which is owned by us) after we've gone.
        long expected = myToken;
        g_iaCallbackToken.compare_exchange_strong(expected, myToken + 1);
    }

    brls::Label* statusLabel() { return status; }

private:
    brls::Label* status  = nullptr;
    long         myToken = 0;
};

static void pushConsoleGameList(const std::string& consoleName);

static void openCustomConsole(const std::string& consoleName, const std::string& iaId) {
    long  myToken     = g_iaCallbackToken.fetch_add(1) + 1;
    auto* view        = new IaLoadingView(consoleName, myToken);
    auto* activity    = new brls::Activity(view);
    auto* statusLabel = view->statusLabel();
    auto  consoleNameCopy = consoleName;

    brls::Application::getRunLoopEvent()->subscribe(
        [statusLabel, consoleNameCopy, myToken]() {
            if (g_iaCallbackToken.load() != myToken) return;
            IaState s = g_iaState.load();
            if (s == IaState::Ready) {
                g_iaCallbackToken.fetch_add(1); // mark self inactive
                brls::Application::popActivity(brls::TransitionAnimation::FADE);
                pushConsoleGameList(consoleNameCopy);
                return;
            }
            if (s == IaState::Error) {
                g_iaCallbackToken.fetch_add(1);
                statusLabel->setText("Failed to fetch metadata. Press B to return.");
                return;
            }
        });

    brls::Application::pushActivity(activity);
    activity->registerAction(
        "Back", brls::ControllerButton::BUTTON_B,
        [](brls::View*) -> bool {
            brls::Application::popActivity(brls::TransitionAnimation::FADE);
            return true;
        });
    startIaLoad(consoleName.c_str(), iaId.c_str());
}

static void pushConsoleGameList(const std::string& consoleName) {
    auto* tabContent  = buildGameListTab(consoleName.c_str());
    auto* appletFrame = new brls::AppletFrame(tabContent);
    appletFrame->setTitle(consoleName);
    auto* activity    = new brls::Activity(appletFrame);
    brls::Application::pushActivity(activity);
}

class ConsoleAdminView : public brls::ScrollingFrame {
public:
    ConsoleAdminView() {
        container = new brls::Box(brls::Axis::COLUMN);
        container->setPadding(24.0f);
        this->setContentView(container);
        rebuild();
    }

    void rebuild() {
        container->clearViews(true);

        auto* addCell = new brls::DetailCell();
        addCell->setText("+ Add new console");
        addCell->setDetailText("Console name, Internet Archive ID, install path");
        addCell->setFocusable(true);
        addCell->registerClickAction([this](brls::View*) -> bool {
            beginAddFlow();
            return true;
        });
        container->addView(addCell);

        if (custom_console_count == 0) {
            auto* hint = new brls::Label();
            hint->setText("No custom consoles yet.");
            hint->setFontSize(18.0f);
            hint->setTextColor(nvgRGB(0xB0, 0xB0, 0xB8));
            hint->setMarginTop(20.0f);
            hint->setMarginLeft(8.0f);
            container->addView(hint);
            return;
        }

        for (int i = 0; i < custom_console_count; i++) {
            CustomConsole* c = &custom_consoles[i];
            int idx = i;

            auto* cell = new brls::DetailCell();
            cell->setText(c->name);
            char detail[512];
            snprintf(detail, sizeof(detail), "IA: %s   path: %s", c->ia_id, c->path);
            cell->setDetailText(detail);
            cell->setFocusable(true);

            std::string name = c->name;
            std::string iaId = c->ia_id;
            cell->registerClickAction([name, iaId](brls::View*) -> bool {
                openCustomConsole(name, iaId);
                return true;
            });

            cell->registerAction(
                "Delete", brls::ControllerButton::BUTTON_X,
                [this, idx, name](brls::View*) -> bool {
                    auto* dialog = new brls::Dialog(
                        "Remove console \"" + name + "\"?");
                    dialog->addButton("Remove", [this, idx]() {
                        ia_remove_console(idx);
                        rebuild();
                    });
                    dialog->addButton("Cancel", []() {});
                    dialog->open();
                    return true;
                });

            container->addView(cell);
        }
    }

private:
    void beginAddFlow() {
        auto tempName = std::make_shared<std::string>();
        auto tempId   = std::make_shared<std::string>();

        brls::Application::getImeManager()->openForText(
            [this, tempName, tempId](std::string name) {
                if (name.empty()) return;
                *tempName = name;
                brls::Application::getImeManager()->openForText(
                    [this, tempName, tempId](std::string iaId) {
                        if (iaId.empty()) return;
                        *tempId = iaId;
                        brls::Application::getImeManager()->openForText(
                            [this, tempName, tempId](std::string path) {
                                if (path.empty()) return;
                                if (ia_add_console(tempName->c_str(),
                                                   tempId->c_str(),
                                                   path.c_str())) {
                                    rebuild();
                                } else {
                                    auto* dialog = new brls::Dialog(
                                        "Could not add console (limit reached).");
                                    dialog->addButton("OK", []() {});
                                    dialog->open();
                                }
                            },
                            "Install path (e.g. /dump/snes)", "", 255, "");
                    },
                    "Internet Archive ID (e.g. retro-snes)", "", 127, "");
            },
            "Console name (e.g. SNES)", "", 63, "");
    }

    brls::Box* container = nullptr;
};

static brls::View* buildAddConsoleTab() {
    return new ConsoleAdminView();
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

static auto makeExitDialogHandler() {
    return [](brls::View*) -> bool {
        auto* dialog = new brls::Dialog("Exit NexPS?");
        dialog->addButton("Yes", []() { brls::Application::quit(); });
        dialog->addButton("Cancel", []() {});
        dialog->open();
        return true;
    };
}

static void registerExitDialog(brls::Activity* activity) {
    activity->registerAction(
        "Exit", brls::ControllerButton::BUTTON_START,
        makeExitDialogHandler());
}

// Same as registerExitDialog but also catches B so the user can't
// accidentally unwind back to the splash activity sitting underneath.
static void registerMainExitGuards(brls::Activity* activity) {
    activity->registerAction(
        "Exit", brls::ControllerButton::BUTTON_START,
        makeExitDialogHandler());
    activity->registerAction(
        "Exit", brls::ControllerButton::BUTTON_B,
        makeExitDialogHandler(),
        /*hidden=*/true);
}

static brls::Activity* makeMainActivity() {
    auto* tabFrame = new brls::TabFrame();

    tabFrame->addTab("PSP", []() { return buildGameListTab("PSP"); });
    tabFrame->addTab("PSX", []() { return buildGameListTab("PSX"); });
    tabFrame->addSeparator();
    tabFrame->addTab("+ Add Console", []() { return buildAddConsoleTab(); });
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

    brls::Application::getRunLoopEvent()->subscribe([]() {
        if (g_dlView) g_dlView->tick();
    });

    static bool swapped = false;
    brls::Application::getRunLoopEvent()->subscribe([statusLabel]() {
        if (swapped) return;

        DbState state = g_dbState.load();
        int     step  = g_dbStep.load();

        if (state == DbState::Ready) {
            auto* mainActivity = makeMainActivity();
            brls::Application::popActivity(brls::TransitionAnimation::FADE);
            brls::Application::pushActivity(mainActivity);
            // Activity::registerAction delegates to contentView, which is only
            // populated inside pushActivity — registering before push is a no-op.
            registerMainExitGuards(mainActivity);
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
