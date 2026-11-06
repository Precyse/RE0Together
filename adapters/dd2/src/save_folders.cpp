#include "save_folders.h"

#include <windows.h>

#include "config.h"
#include "paths.h"
#include "text.h"

namespace {

constexpr const wchar_t* kSessionFolder = L"\\session";
constexpr const wchar_t* kCampaignsFolder = L"\\campaigns";
constexpr const wchar_t* kOwnProfileFolder = L"\\profile";
constexpr const wchar_t* kProfileFile = L"\\profile.dd2p";
// The SaveService mount is the save folder itself (the game's own reads back "./win64_save"), so a world's saves sit in
// <world>\win64_save, where the launcher puts them (games/dd2.json).
constexpr const wchar_t* kSaveFolder = L"\\win64_save";

std::wstring created(const std::wstring& folder) {
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring campaignFolder(const std::string& id) {
    return created(created(coopDirectory() + kCampaignsFolder) + L"\\" + text::utf16(id));
}

}  // namespace

namespace save_folders {

Folders of(const Config& config) {
    const std::wstring ownProfile = created(coopDirectory() + kOwnProfileFolder) + kProfileFile;
    if (config.captureSlot >= 0) {
        if (config.arisen.empty()) return {std::nullopt, ownProfile};
        const std::wstring campaign = campaignFolder(config.arisen);
        return {created(campaign + kSaveFolder), campaign + kProfileFile};
    }
    if (config.coop && config.guest) {
        const std::wstring character = config.arisen.empty() ? ownProfile : campaignFolder(config.arisen) + kProfileFile;
        return {created(created(coopDirectory() + kSessionFolder) + kSaveFolder), character};
    }
    if (config.coop && !config.campaign.empty()) {
        const std::wstring campaign = campaignFolder(config.campaign);
        return {created(campaign + kSaveFolder), campaign + kProfileFile};
    }
    return {std::nullopt, ownProfile};
}

}  // namespace save_folders
