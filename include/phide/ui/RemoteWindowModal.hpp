#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

namespace remote {

struct SshHostEntry {
    std::wstring alias;
    std::wstring hostName;
    std::wstring user;
    std::wstring port;
    std::wstring identityFile;

    [[nodiscard]] std::wstring TargetSpec() const {
        if (!alias.empty()) return alias;
        if (!user.empty() && !hostName.empty()) return user + L"@" + hostName;
        return hostName;
    }

    [[nodiscard]] std::wstring DisplayDetail() const {
        std::wstring d;
        if (!user.empty()) d += user + L"@";
        d += (!hostName.empty() ? hostName : alias);
        if (!port.empty() && port != L"22") d += L":" + port;
        return d;
    }
};

struct WslDistroEntry {
    std::wstring name;
    std::wstring state;
    std::wstring version;
    bool isDefault = false;
};

struct DevContainerEntry {
    std::wstring id;
    std::wstring name;
    std::wstring image;
    std::wstring status;
    bool isRunning = false;
};

struct RemoteFileEntry {
    std::wstring name;
    std::wstring fullPath;
    bool isDir = false;
    uint64_t sizeBytes = 0;
    int depth = 0;
    bool isExpanded = false;
    bool childrenLoaded = false;
};

enum class RemoteTargetFilter {
    All = 0,
    Ssh = 1,
    Wsl = 2,
    DevContainers = 3
};

[[nodiscard]] std::wstring GetSshConfigPath();
[[nodiscard]] std::wstring GetDefaultSshKeyPath();
[[nodiscard]] bool HasSshClientTool();
[[nodiscard]] bool HasScpClientTool();
[[nodiscard]] bool HasWslTool();
[[nodiscard]] bool HasDockerTool();
[[nodiscard]] bool HasSshKeyPair();

[[nodiscard]] std::wstring RunSilentProcessOutput(const std::wstring& commandLine, DWORD timeoutMs = 4500, DWORD* outExitCode = nullptr);
[[nodiscard]] std::vector<SshHostEntry> LoadSshConfigHosts();
[[nodiscard]] bool ParseAdhocSshInput(const std::wstring& rawInput, SshHostEntry& outHost);
[[nodiscard]] bool SaveSshConfigHost(const std::wstring& rawInput, SshHostEntry& outSavedHost, std::wstring& outError);
bool EnsureSshConfigExists();

[[nodiscard]] std::vector<WslDistroEntry> ProbeWslDistros(std::wstring* outStatusMsg = nullptr);
[[nodiscard]] std::vector<DevContainerEntry> ProbeDockerContainers(std::wstring* outStatusMsg = nullptr);

[[nodiscard]] std::wstring BuildInteractiveSshCommand(const SshHostEntry& host, const std::wstring& initialRemoteDir = L"");
[[nodiscard]] std::wstring BuildInteractiveWslCommand(const std::wstring& distroName);
[[nodiscard]] std::wstring BuildInteractiveDockerCommand(const std::wstring& containerId, bool startFirst = false);

[[nodiscard]] std::vector<RemoteFileEntry> FetchRemoteSshDirectory(
    const SshHostEntry& host,
    const std::wstring& remotePath,
    std::wstring& outResolvedPath,
    std::wstring& outError,
    int depth = 0
);

[[nodiscard]] bool DownloadRemoteSshFile(
    const SshHostEntry& host,
    const std::wstring& remotePath,
    const std::wstring& localCachePath,
    std::wstring& outError
);

[[nodiscard]] bool UploadRemoteSshFile(
    const SshHostEntry& host,
    const std::wstring& localCachePath,
    const std::wstring& remotePath,
    std::wstring& outError
);

} // namespace remote

enum class RemoteModalMode {
    MainMenu = 0,
    SshHostPicker = 1,
    AddSshHost = 2,
    RemotePathInput = 3,
    ContainerPicker = 4,
    WslDistroPicker = 5
};

struct RemoteWindowCallbacks {
    std::function<void(const remote::SshHostEntry& host, const std::wstring& initialDir)> onConnectSshTerminal;
    std::function<void(const remote::SshHostEntry& host, const std::wstring& remotePath)> onBrowseSshFolder;
    std::function<void()> onOpenSshConfig;
    std::function<void()> onSetupSshKey;
    std::function<void(const std::wstring& distroName, bool openFolderInExplorer)> onConnectWsl;
    std::function<void()> onInstallWsl;
    std::function<void(const remote::DevContainerEntry& container)> onAttachContainer;
    std::function<void()> onOpenOrCreateDevContainer;
    std::function<void()> onTargetsUpdated;
    std::function<void()> onRequestRepaint;
};

struct RemotePopupRow {
    std::wstring id;
    std::wstring label;
    std::wstring categoryTag;
    std::wstring detail;
    int group = 0;          ///< Group index (horizontal separator drawn when group changes)
    bool isAccentAction = false;
    D2D1_RECT_F bounds{};
};

/**
 * @class RemoteWindowModal
 * @brief Floating Remote Window Quick Pick overlay triggered from Remote Explorer sidebar or Status Bar >< indicator.
 */
class RemoteWindowModal {
public:
    RemoteWindowModal() = default;
    ~RemoteWindowModal() = default;

    void SetCallbacks(RemoteWindowCallbacks callbacks) { m_callbacks = std::move(callbacks); }

    void Open(RemoteModalMode initialMode = RemoteModalMode::MainMenu, bool sshCurrentWindow = false);
    void OpenForSshBrowse(const remote::SshHostEntry& host);
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] RemoteModalMode GetMode() const noexcept { return m_mode; }
    [[nodiscard]] const D2D1_RECT_F& GetModalBounds() const noexcept { return m_modalBounds; }

    void SetCachedTargets(
        std::vector<remote::SshHostEntry> sshHosts,
        std::vector<remote::WslDistroEntry> wslDistros,
        std::wstring wslStatus,
        std::vector<remote::DevContainerEntry> containers,
        std::wstring containerStatus
    );

    void Render(
        ID2D1RenderTarget* pRT,
        const Theme& theme,
        const phide::app::AppConfig& config,
        float clientWidth,
        float clientHeight
    );

    void HandleMouseMove(POINT pt);
    bool HandleLButtonDown(POINT pt);
    bool HandleMouseWheel(short delta);
    bool HandleChar(wchar_t ch);
    bool HandleKeyDown(UINT vk, bool isCtrl, bool isShift);

    [[nodiscard]] bool HitTestModal(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestInput(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestInteractive(POINT pt) const noexcept;

    // Test & programmatic inspection helpers
    [[nodiscard]] const std::vector<RemotePopupRow>& GetVisibleRows() const noexcept { return m_rows; }
    [[nodiscard]] int GetSelectedIdx() const noexcept { return m_selectedIdx; }
    [[nodiscard]] const std::wstring& GetInputText() const noexcept { return m_inputText; }
    void SetInputTextForTest(const std::wstring& text);
    void RebuildRows();

private:
    void EnsureTextFormats(const Theme& theme, const phide::app::AppConfig& config);
    void ExecuteRow(int rowIdx);
    void MoveSelection(int delta);
    void TriggerAsyncTargetRefresh();

    bool m_isOpen = false;
    RemoteModalMode m_mode = RemoteModalMode::MainMenu;
    bool m_sshCurrentWindow = false;
    remote::SshHostEntry m_pendingSshHost{};

    std::wstring m_inputText;
    std::wstring m_statusBanner;
    bool m_statusIsError = false;

    std::vector<remote::SshHostEntry> m_sshHosts;
    std::vector<remote::WslDistroEntry> m_wslDistros;
    std::wstring m_wslStatus;
    std::vector<remote::DevContainerEntry> m_containers;
    std::wstring m_containerStatus;

    std::vector<RemotePopupRow> m_rows;
    int m_selectedIdx = 0;
    int m_hoveredIdx = -1;
    int m_scrollOffset = 0;
    bool m_hoverBackBtn = false;

    D2D1_RECT_F m_modalBounds{};
    D2D1_RECT_F m_inputBounds{};
    D2D1_RECT_F m_backBtnBounds{};
    float m_lastClientW = 1280.0f;
    float m_lastClientH = 800.0f;
    float m_lastDpiScale = 1.0f;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtRowLabel;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtRowTag;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtInput;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtSmall;

    LucideIcons m_lucide;
    RemoteWindowCallbacks m_callbacks;
};

} // namespace phide::ui
