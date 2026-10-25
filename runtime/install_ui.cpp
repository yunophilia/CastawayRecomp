// First-run installer: asks for the disc image and installs the game data from it. Dialogs
// are SDL's (native message boxes and file picker); the progress is drawn by the launcher.
#include "install_ui.h"
#include "install/install.h"
#include "install/releases.h"
#include "log.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace
{
constexpr const char* kTitle = "CastawayRecomp";

enum class Answer { Yes, No, Cancel };

// buttons: "yes|no|cancel", "ok|cancel" or "ok"
Answer Ask(SDL_Window* parent, SDL_MessageBoxFlags icon, const std::string& text, const char* buttons)
{
    std::string b = buttons;
    std::vector<SDL_MessageBoxButtonData> bd;
    auto add = [&](const char* id, int value, const char* label, SDL_MessageBoxButtonFlags f) {
        if (b.find(id) != std::string::npos) bd.push_back({ f, value, label });
    };
    add("yes", (int)Answer::Yes, "Yes", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
    add("ok", (int)Answer::Yes, "OK", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
    add("no", (int)Answer::No, "No", 0);
    add("cancel", (int)Answer::Cancel, "Cancel", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
    SDL_MessageBoxData data{ icon, parent, kTitle, text.c_str(), (int)bd.size(), bd.data(), nullptr };
    int pressed = (int)Answer::Cancel;
    if (!SDL_ShowMessageBox(&data, &pressed)) return Answer::Cancel;
    return (Answer)pressed;
}

// The native file picker; SDL reports the choice through a callback while events are pumped.
bool PickImage(SDL_Window* parent, fs::path& out)
{
    struct Pick { std::atomic<bool> done{ false }; std::string path; } pick;
    static const SDL_DialogFileFilter filters[] = { { "Wii disc images", "iso;rvz;wia;wbfs;ciso;gcz;nfs" }, { "All files", "*" } };
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            auto* p = (Pick*)user;
            if (files && files[0]) p->path = files[0];
            p->done = true;
        },
        &pick, parent, filters, 2, nullptr, false);
    while (!pick.done)
    {
        SDL_PumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (pick.path.empty()) return false;
    out = fs::u8path(pick.path);
    return true;
}

InstallResult RunWithProgress(const fs::path& image, const fs::path& dest, std::string& error, const InstallProgressFn& draw)
{
    InstallProgress progress;
    InstallResult result = InstallResult::Canceled;
    std::atomic<bool> done{ false };
    std::thread worker([&] {
        result = Install_Run(image, dest, progress, error);
        done = true;
    });
    std::string line = "Installing the game data from " + image.filename().string() + "...";
    while (!done)
    {
        double f = progress.total ? (double)progress.done / progress.total : 0.0;
        if (!draw(line.c_str(), f)) progress.cancel = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    return result;
}
}   // namespace

bool InstallUI_Run(SDL_Window* parent, const fs::path& dest, const std::string& foundImage, const std::string& damage,
                   const InstallProgressFn& draw)
{
    std::string why = damage.empty() ? "The game data is not installed yet."
                                     : "The installed game data is damaged (" + damage + ") and needs reinstalling.";
    fs::path image;
    if (!foundImage.empty() && Release_Find(Install_GameId(foundImage)))
    {
        Answer a = Ask(parent, SDL_MESSAGEBOX_INFORMATION,
                       why + " Install it from\n\n" + foundImage + "?\n\nChoose No to select a different disc image.", "yes|no|cancel");
        if (a == Answer::Cancel) return false;
        if (a == Answer::Yes) image = fs::u8path(foundImage);
    }
    else if (Ask(parent, SDL_MESSAGEBOX_INFORMATION,
                 why + "\n\nSelect your own disc image of The Sims 2: Castaway (USA) to install it. ISO, RVZ, WIA and WBFS "
                       "images work. This takes a minute and needs about 1.3 GB; the image is not needed afterwards.",
                 "ok|cancel") != Answer::Yes)
        return false;
    for (;;)
    {
        if (image.empty() && !PickImage(parent, image)) return false;
        LOG_INFO("installing game data from {} to {}", image.string(), dest.string());
        std::string error;
        InstallResult r = RunWithProgress(image, dest, error, draw);
        if (r == InstallResult::Ok)
        {
            LOG_INFO("game data installed");
            return true;
        }
        if (r == InstallResult::Canceled) return false;
        LOG_ERROR("install failed: {}", error);
        std::string msg = "Could not install the game data:\n\n" + error;
        if (r == InstallResult::WriteFailed)
        {
            Ask(parent, SDL_MESSAGEBOX_ERROR, msg, "ok");
            return false;
        }
        if (Ask(parent, SDL_MESSAGEBOX_ERROR, msg + "\n\nSelect a different disc image?", "ok|cancel") != Answer::Yes) return false;
        image.clear();
    }
}
