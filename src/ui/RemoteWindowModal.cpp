#include "phide/ui/RemoteWindowModal.hpp"
#include <shlobj.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>

namespace phide::ui {

namespace remote {

static std::wstring TrimWS(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r' || s.front() == L'\n')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n')) {
        s.pop_back();
    }
    return s;
}

static std::wstring StripQuotes(std::wstring s) {
    s = TrimWS(std::move(s));
    if (s.size() >= 2 && ((s.front() == L'"' && s.back() == L'"') || (s.front() == L'\'' && s.back() == L'\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

static std::wstring ToLowerCopy(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], len);
    return out;
}

static std::wstring DecodeProcessBytes(const std::string& bytes) {
    if (bytes.empty()) return L"";

    // Detect UTF-16LE BOM or Windows console UTF-16LE (e.g. wsl.exe --list --verbose)
    bool hasUtf16Bom = (bytes.size() >= 2 &&
                        static_cast<unsigned char>(bytes[0]) == 0xFF &&
                        static_cast<unsigned char>(bytes[1]) == 0xFE);
    bool looksUtf16Le = hasUtf16Bom;
    if (!looksUtf16Le && bytes.size() >= 4 && (bytes.size() % 2 == 0)) {
        size_t zeroOdd = 0;
        size_t pairs = (std::min<size_t>)(bytes.size() / 2, 64);
        for (size_t i = 0; i < pairs; ++i) {
            if (bytes[i * 2 + 1] == '\0' && bytes[i * 2] != '\0') zeroOdd++;
        }
        if (zeroOdd >= (pairs * 3) / 4 && zeroOdd >= 2) {
            looksUtf16Le = true;
        }
    }

    if (looksUtf16Le) {
        size_t offset = hasUtf16Bom ? 2 : 0;
        size_t wcharCount = (bytes.size() - offset) / sizeof(wchar_t);
        std::wstring result(wcharCount, L'\0');
        memcpy(&result[0], bytes.data() + offset, wcharCount * sizeof(wchar_t));
        // Remove any embedded NUL characters
        result.erase(std::remove(result.begin(), result.end(), L'\0'), result.end());
        return result;
    }

    // Strip UTF-8 BOM if present
    size_t start = (bytes.size() >= 3 &&
                    static_cast<unsigned char>(bytes[0]) == 0xEF &&
                    static_cast<unsigned char>(bytes[1]) == 0xBB &&
                    static_cast<unsigned char>(bytes[2]) == 0xBF) ? 3 : 0;
    return Utf8ToWide(bytes.substr(start));
}

static std::wstring QuoteWinArg(const std::wstring& arg) {
    std::wstring escaped = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') escaped += L"\\\"";
        else escaped += c;
    }
    escaped += L"\"";
    return escaped;
}

static std::wstring QuotePosixSingle(const std::wstring& s) {
    std::wstring out = L"'";
    for (wchar_t c : s) {
        if (c == L'\'') out += L"'\\''";
        else out += c;
    }
    out += L"'";
    return out;
}

static bool ToolExistsOnSystem(const wchar_t* exeName, const wchar_t* knownFullPath = nullptr) {
    std::error_code ec;
    if (knownFullPath && *knownFullPath && std::filesystem::exists(knownFullPath, ec)) {
        return true;
    }
    wchar_t found[MAX_PATH]{};
    return SearchPathW(nullptr, exeName, nullptr, MAX_PATH, found, nullptr) > 0;
}

static std::wstring ResolveExePath(const wchar_t* exeName, const wchar_t* knownFullPath = nullptr) {
    std::error_code ec;
    if (knownFullPath && *knownFullPath && std::filesystem::exists(knownFullPath, ec)) {
        return knownFullPath;
    }
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH, found, nullptr) > 0) {
        return found;
    }
    return exeName;
}

std::wstring GetUserHomeDir() {
    PWSTR profile = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)) && profile) {
        std::wstring res = profile;
        CoTaskMemFree(profile);
        return res;
    }
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return buf;
    return L"C:\\Users";
}

std::wstring GetSshConfigPath() {
    return GetUserHomeDir() + L"\\.ssh\\config";
}

std::wstring GetDefaultSshKeyPath() {
    return GetUserHomeDir() + L"\\.ssh\\id_ed25519";
}

bool HasSshClientTool() {
    return ToolExistsOnSystem(L"ssh.exe", L"C:\\Windows\\System32\\OpenSSH\\ssh.exe");
}

bool HasScpClientTool() {
    return ToolExistsOnSystem(L"scp.exe", L"C:\\Windows\\System32\\OpenSSH\\scp.exe");
}

bool HasWslTool() {
    return ToolExistsOnSystem(L"wsl.exe", L"C:\\Windows\\System32\\wsl.exe");
}

bool HasDockerTool() {
    return ToolExistsOnSystem(L"docker.exe", L"C:\\Program Files\\Docker\\Docker\\resources\\bin\\docker.exe");
}

bool HasSshKeyPair() {
    std::wstring sshDir = GetUserHomeDir() + L"\\.ssh\\";
    std::error_code ec;
    const wchar_t* keys[] = { L"id_ed25519", L"id_rsa", L"id_ecdsa", L"id_ed25519.pub", L"id_rsa.pub" };
    for (const auto* k : keys) {
        if (std::filesystem::exists(sshDir + k, ec)) return true;
    }
    return false;
}

std::wstring RunSilentProcessOutput(const std::wstring& commandLine, DWORD timeoutMs, DWORD* outExitCode) {
    if (outExitCode) *outExitCode = 1;
    if (commandLine.empty()) return L"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return L"";
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    // Separate write handle for nul stdin so tools waiting for input exit immediately
    HANDLE hNulRead = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = (hNulRead != INVALID_HANDLE_VALUE) ? hNulRead : NULL;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(commandLine.begin(), commandLine.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(hWrite);
    if (hNulRead != INVALID_HANDLE_VALUE) CloseHandle(hNulRead);

    if (!ok) {
        CloseHandle(hRead);
        return L"";
    }

    std::string rawOutput;
    char buf[4096];
    ULONGLONG startTick = GetTickCount64();
    bool timedOut = false;

    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD toRead = (std::min<DWORD>)(avail, sizeof(buf));
            DWORD bytesRead = 0;
            if (ReadFile(hRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                rawOutput.append(buf, bytesRead);
                continue;
            }
        }

        DWORD waitRes = WaitForSingleObject(pi.hProcess, 25);
        if (waitRes == WAIT_OBJECT_0) {
            // Drain remaining pipe data after exit
            for (;;) {
                DWORD rem = 0;
                if (!PeekNamedPipe(hRead, nullptr, 0, nullptr, &rem, nullptr) || rem == 0) break;
                DWORD toRead = (std::min<DWORD>)(rem, sizeof(buf));
                DWORD bytesRead = 0;
                if (!ReadFile(hRead, buf, toRead, &bytesRead, nullptr) || bytesRead == 0) break;
                rawOutput.append(buf, bytesRead);
            }
            break;
        }

        if ((GetTickCount64() - startTick) >= timeoutMs) {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    DWORD exitCode = 1;
    if (!timedOut) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }
    if (outExitCode) *outExitCode = exitCode;

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return DecodeProcessBytes(rawOutput);
}

std::vector<SshHostEntry> LoadSshConfigHosts() {
    std::vector<SshHostEntry> hosts;
    std::wstring cfgPath = GetSshConfigPath();
    std::error_code ec;
    if (!std::filesystem::exists(cfgPath, ec)) return hosts;

    std::ifstream ifs(cfgPath, std::ios::binary);
    if (!ifs.is_open()) return hosts;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::wstring wcontent = DecodeProcessBytes(content);
    std::wstringstream ss(wcontent);
    std::wstring line;

    SshHostEntry current{};
    bool hasCurrent = false;
    bool skipCurrent = false;

    auto flushCurrent = [&]() {
        if (hasCurrent && !skipCurrent && !current.alias.empty()) {
            if (current.hostName.empty()) current.hostName = current.alias;
            hosts.push_back(current);
        }
        current = SshHostEntry{};
        hasCurrent = false;
        skipCurrent = false;
    };

    while (std::getline(ss, line)) {
        line = TrimWS(line);
        if (line.empty() || line.front() == L'#') continue;

        // Strip trailing inline comments outside quotes
        bool inQuotes = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == L'"') inQuotes = !inQuotes;
            else if (line[i] == L'#' && !inQuotes) {
                line = TrimWS(line.substr(0, i));
                break;
            }
        }
        if (line.empty()) continue;

        // Split key and value (whitespace or '=')
        size_t sep = line.find_first_of(L" \t=");
        if (sep == std::wstring::npos) continue;
        std::wstring key = ToLowerCopy(TrimWS(line.substr(0, sep)));
        std::wstring val = TrimWS(line.substr(sep + 1));
        if (!val.empty() && val.front() == L'=') val = TrimWS(val.substr(1));

        if (key == L"host") {
            flushCurrent();
            // Handle first host pattern on the Host line (skip wildcards like *)
            std::wstringstream hs(val);
            std::wstring firstToken;
            hs >> firstToken;
            firstToken = StripQuotes(firstToken);
            if (!firstToken.empty()) {
                hasCurrent = true;
                if (firstToken.find(L'*') != std::wstring::npos || firstToken.find(L'?') != std::wstring::npos) {
                    skipCurrent = true;
                } else {
                    current.alias = firstToken;
                }
            }
        } else if (hasCurrent && !skipCurrent) {
            val = StripQuotes(val);
            if (key == L"hostname") current.hostName = val;
            else if (key == L"user") current.user = val;
            else if (key == L"port") current.port = val;
            else if (key == L"identityfile") current.identityFile = val;
        }
    }
    flushCurrent();
    return hosts;
}

bool ParseAdhocSshInput(const std::wstring& rawInput, SshHostEntry& outHost) {
    outHost = SshHostEntry{};
    std::wstring trimmed = TrimWS(rawInput);
    if (trimmed.empty()) return false;

    // Tokenize respecting quotes
    std::vector<std::wstring> tokens;
    std::wstring cur;
    bool inQuotes = false;
    wchar_t quoteChar = 0;
    for (wchar_t c : trimmed) {
        if (inQuotes) {
            if (c == quoteChar) inQuotes = false;
            else cur += c;
        } else if (c == L'"' || c == L'\'') {
            inQuotes = true;
            quoteChar = c;
        } else if (c == L' ' || c == L'\t') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    if (tokens.empty()) return false;

    size_t idx = 0;
    std::wstring lowerFirst = ToLowerCopy(tokens[0]);
    if (lowerFirst == L"ssh" || lowerFirst == L"ssh.exe") {
        idx = 1;
    }

    std::wstring target;
    while (idx < tokens.size()) {
        const std::wstring& t = tokens[idx];
        if ((t == L"-p" || t == L"--port") && idx + 1 < tokens.size()) {
            outHost.port = tokens[++idx];
        } else if (t == L"-i" && idx + 1 < tokens.size()) {
            outHost.identityFile = tokens[++idx];
        } else if (t == L"-l" && idx + 1 < tokens.size()) {
            outHost.user = tokens[++idx];
        } else if (t == L"-A" || t == L"-t" || t == L"-X" || t == L"-Y" || t == L"-v") {
            // common ssh boolean flags
        } else if (!t.empty() && t.front() == L'-') {
            if (t.size() == 2 && idx + 1 < tokens.size()) idx++; // skip option arg
        } else if (target.empty()) {
            target = t;
        }
        idx++;
    }

    if (target.empty()) return false;

    // Strip ssh:// prefix if pasted
    if (ToLowerCopy(target).rfind(L"ssh://", 0) == 0) {
        target = target.substr(6);
    }

    // Parse user@host:port
    size_t atPos = target.find(L'@');
    if (atPos != std::wstring::npos) {
        if (outHost.user.empty()) outHost.user = target.substr(0, atPos);
        target = target.substr(atPos + 1);
    }

    size_t colonPos = target.rfind(L':');
    if (colonPos != std::wstring::npos && target.find(L']') == std::wstring::npos) {
        std::wstring maybePort = target.substr(colonPos + 1);
        bool allDigits = !maybePort.empty() && std::all_of(maybePort.begin(), maybePort.end(), iswdigit);
        if (allDigits) {
            if (outHost.port.empty()) outHost.port = maybePort;
            target = target.substr(0, colonPos);
        }
    }

    target = TrimWS(target);
    if (target.empty()) return false;

    outHost.hostName = target;
    outHost.alias = outHost.user.empty() ? target : (target);
    // Avoid whitespace or dangerous characters in hostName
    for (wchar_t c : outHost.hostName) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'"' || c == L';' || c == L'&' || c == L'|') {
            return false;
        }
    }
    return true;
}

bool EnsureSshConfigExists() {
    std::wstring sshDir = GetUserHomeDir() + L"\\.ssh";
    std::error_code ec;
    std::filesystem::create_directories(sshDir, ec);
    std::wstring cfgPath = sshDir + L"\\config";
    if (!std::filesystem::exists(cfgPath, ec)) {
        std::ofstream ofs(cfgPath, std::ios::binary);
        if (!ofs.is_open()) return false;
        ofs << "# Phide Remote-SSH Configuration (~/.ssh/config)\n"
               "# Example:\n"
               "# Host my-server\n"
               "#     HostName 192.168.1.50\n"
               "#     User ubuntu\n"
               "#     Port 22\n"
               "#     IdentityFile ~/.ssh/id_ed25519\n";
    }
    return true;
}

bool SaveSshConfigHost(const std::wstring& rawInput, SshHostEntry& outSavedHost, std::wstring& outError) {
    outError.clear();
    if (!ParseAdhocSshInput(rawInput, outSavedHost)) {
        outError = L"Invalid target. Try 'user@host' or 'ssh user@192.168.1.10 -p 22'.";
        return false;
    }

    if (!EnsureSshConfigExists()) {
        outError = L"Failed to create %USERPROFILE%\\.ssh\\config.";
        return false;
    }

    auto existing = LoadSshConfigHosts();
    std::wstring baseAlias = outSavedHost.hostName;
    // Also support alias if user typed something distinct; avoid collision
    std::wstring candidateAlias = baseAlias;
    int suffix = 2;
    auto aliasExists = [&](const std::wstring& name) {
        for (const auto& h : existing) {
            if (_wcsicmp(h.alias.c_str(), name.c_str()) == 0) return true;
        }
        return false;
    };

    // If same hostName + user + port already exists, avoid creating duplicate noise unless alias differs
    for (const auto& h : existing) {
        if (_wcsicmp(h.hostName.c_str(), outSavedHost.hostName.c_str()) == 0 &&
            h.user == outSavedHost.user &&
            h.port == outSavedHost.port) {
            outSavedHost = h;
            return true;
        }
    }

    while (aliasExists(candidateAlias)) {
        candidateAlias = baseAlias + L"-" + std::to_wstring(suffix++);
    }
    outSavedHost.alias = candidateAlias;

    std::wstring cfgPath = GetSshConfigPath();
    std::ofstream ofs(cfgPath, std::ios::app | std::ios::binary);
    if (!ofs.is_open()) {
        outError = L"Could not open ~/.ssh/config for writing.";
        return false;
    }

    std::ostringstream block;
    block << "\nHost " << WideToUtf8(outSavedHost.alias) << "\n";
    block << "    HostName " << WideToUtf8(outSavedHost.hostName) << "\n";
    if (!outSavedHost.user.empty()) {
        block << "    User " << WideToUtf8(outSavedHost.user) << "\n";
    }
    if (!outSavedHost.port.empty()) {
        block << "    Port " << WideToUtf8(outSavedHost.port) << "\n";
    }
    if (!outSavedHost.identityFile.empty()) {
        block << "    IdentityFile " << WideToUtf8(outSavedHost.identityFile) << "\n";
    }
    ofs << block.str();
    ofs.flush();
    return true;
}

std::vector<WslDistroEntry> ProbeWslDistros(std::wstring* outStatusMsg) {
    if (outStatusMsg) outStatusMsg->clear();
    std::vector<WslDistroEntry> distros;
    if (!HasWslTool()) {
        if (outStatusMsg) *outStatusMsg = L"WSL executable not found on this system.";
        return distros;
    }

    DWORD exitCode = 1;
    std::wstring wslExe = ResolveExePath(L"wsl.exe", L"C:\\Windows\\System32\\wsl.exe");
    std::wstring out = RunSilentProcessOutput(QuoteWinArg(wslExe) + L" --list --verbose", 3000, &exitCode);
    std::wstring lower = ToLowerCopy(out);

    if (lower.find(L"no installed distributions") != std::wstring::npos ||
        lower.find(L"has no installed distributions") != std::wstring::npos) {
        if (outStatusMsg) *outStatusMsg = L"WSL has no installed distributions. Install Ubuntu via 'wsl --install'.";
        return distros;
    }
    if (lower.find(L"is not installed") != std::wstring::npos ||
        lower.find(L"wsl --install") != std::wstring::npos) {
        if (outStatusMsg) *outStatusMsg = L"WSL is not installed yet. Run 'wsl --install' to set it up.";
        return distros;
    }

    std::wstringstream ss(out);
    std::wstring line;
    bool sawHeader = false;
    while (std::getline(ss, line)) {
        std::wstring trimmed = TrimWS(line);
        if (trimmed.empty()) continue;
        std::wstring ltrim = ToLowerCopy(trimmed);
        if (!sawHeader && ltrim.find(L"name") != std::wstring::npos && ltrim.find(L"state") != std::wstring::npos) {
            sawHeader = true;
            continue;
        }
        if (!sawHeader && exitCode != 0) {
            // Skip plain text diagnostics when wsl failed
            continue;
        }

        WslDistroEntry d{};
        if (trimmed.front() == L'*') {
            d.isDefault = true;
            trimmed = TrimWS(trimmed.substr(1));
        }
        std::wstringstream ls(trimmed);
        ls >> d.name >> d.state >> d.version;
        if (!d.name.empty() && !d.state.empty()) {
            distros.push_back(std::move(d));
        }
    }

    if (distros.empty() && outStatusMsg && outStatusMsg->empty()) {
        *outStatusMsg = (exitCode == 0)
            ? L"No WSL distributions registered."
            : L"WSL is not initialized or no distros installed.";
    }
    return distros;
}

std::vector<DevContainerEntry> ProbeDockerContainers(std::wstring* outStatusMsg) {
    if (outStatusMsg) outStatusMsg->clear();
    std::vector<DevContainerEntry> list;
    if (!HasDockerTool()) {
        if (outStatusMsg) *outStatusMsg = L"Docker CLI not detected on PATH. Install Docker Desktop for Dev Containers.";
        return list;
    }

    DWORD exitCode = 1;
    std::wstring dockerExe = ResolveExePath(L"docker.exe", L"C:\\Program Files\\Docker\\Docker\\resources\\bin\\docker.exe");
    std::wstring out = RunSilentProcessOutput(
        QuoteWinArg(dockerExe) + L" ps -a --format \"{{.ID}}|{{.Names}}|{{.Image}}|{{.Status}}|{{.State}}\"",
        3500, &exitCode);

    if (exitCode != 0) {
        std::wstring lower = ToLowerCopy(out);
        if (lower.find(L"daemon") != std::wstring::npos || lower.find(L"pipe") != std::wstring::npos || lower.find(L"error during connect") != std::wstring::npos) {
            if (outStatusMsg) *outStatusMsg = L"Docker daemon is not running. Start Docker Desktop and refresh.";
        } else {
            if (outStatusMsg) *outStatusMsg = L"Unable to query Docker containers.";
        }
        return list;
    }

    std::wstringstream ss(out);
    std::wstring line;
    while (std::getline(ss, line)) {
        line = TrimWS(line);
        if (line.empty()) continue;
        std::vector<std::wstring> cols;
        std::wstringstream ls(line);
        std::wstring part;
        while (std::getline(ls, part, L'|')) cols.push_back(TrimWS(part));
        if (cols.size() >= 4) {
            DevContainerEntry c{};
            c.id = cols[0];
            c.name = cols[1];
            c.image = cols[2];
            c.status = cols[3];
            std::wstring state = cols.size() >= 5 ? ToLowerCopy(cols[4]) : ToLowerCopy(c.status);
            c.isRunning = (state.find(L"running") != std::wstring::npos || state.rfind(L"up", 0) == 0);
            if (!c.id.empty()) list.push_back(std::move(c));
        }
    }

    if (list.empty() && outStatusMsg) {
        *outStatusMsg = L"No Docker containers found. Open or create a .devcontainer configuration.";
    }
    return list;
}

static std::wstring BuildSshBaseArgs(const SshHostEntry& host, bool batchMode, bool forScp) {
    std::wstring args;
    if (batchMode) {
        args += L"-o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new ";
    } else {
        args += L"-o StrictHostKeyChecking=accept-new ";
    }
    if (!host.port.empty()) {
        args += (forScp ? L"-P " : L"-p ") + QuoteWinArg(host.port) + L" ";
    }
    if (!host.identityFile.empty()) {
        args += L"-i " + QuoteWinArg(host.identityFile) + L" ";
    }
    return args;
}

std::wstring BuildInteractiveSshCommand(const SshHostEntry& host, const std::wstring& initialRemoteDir) {
    std::wstring sshExe = ResolveExePath(L"ssh.exe", L"C:\\Windows\\System32\\OpenSSH\\ssh.exe");
    std::wstring cmd = QuoteWinArg(sshExe) + L" ";
    if (!initialRemoteDir.empty()) cmd += L"-t ";
    cmd += BuildSshBaseArgs(host, false, false);
    cmd += QuoteWinArg(host.TargetSpec());
    if (!initialRemoteDir.empty()) {
        std::wstring posixCmd = L"cd " + QuotePosixSingle(initialRemoteDir) + L" && exec ${SHELL:-sh} -l";
        cmd += L" " + QuoteWinArg(posixCmd);
    }
    return cmd;
}

std::wstring BuildInteractiveWslCommand(const std::wstring& distroName) {
    std::wstring wslExe = ResolveExePath(L"wsl.exe", L"C:\\Windows\\System32\\wsl.exe");
    if (distroName.empty()) {
        return QuoteWinArg(wslExe);
    }
    return QuoteWinArg(wslExe) + L" -d " + QuoteWinArg(distroName);
}

std::wstring BuildInteractiveDockerCommand(const std::wstring& containerId, bool startFirst) {
    std::wstring dockerExe = ResolveExePath(L"docker.exe", L"C:\\Program Files\\Docker\\Docker\\resources\\bin\\docker.exe");
    std::wstring execCmd = QuoteWinArg(dockerExe) + L" exec -it " + QuoteWinArg(containerId) +
                           L" sh -c \"command -v bash >/dev/null && exec bash || exec sh\"";
    if (startFirst) {
        return L"cmd.exe /c \"" + QuoteWinArg(dockerExe) + L" start " + QuoteWinArg(containerId) + L" && " + execCmd + L"\"";
    }
    return execCmd;
}

std::vector<RemoteFileEntry> FetchRemoteSshDirectory(
    const SshHostEntry& host,
    const std::wstring& remotePath,
    std::wstring& outResolvedPath,
    std::wstring& outError,
    int depth
) {
    outError.clear();
    outResolvedPath = remotePath.empty() ? L"~" : remotePath;
    std::vector<RemoteFileEntry> entries;
    if (!HasSshClientTool()) {
        outError = L"OpenSSH client (ssh.exe) was not found.";
        return entries;
    }

    std::wstring targetDir = TrimWS(remotePath);
    std::wstring cdExpr;
    if (targetDir.empty() || targetDir == L"~") {
        cdExpr = L"cd ~";
    } else if (targetDir.rfind(L"~/", 0) == 0) {
        cdExpr = L"cd ~/" + QuotePosixSingle(targetDir.substr(2));
    } else {
        cdExpr = L"cd " + QuotePosixSingle(targetDir);
    }

    std::wstring remoteScript = cdExpr + L" && printf '__PHIDE_PWD__:%s\\n' \"$PWD\" && LC_ALL=C ls -1pA";
    std::wstring sshExe = ResolveExePath(L"ssh.exe", L"C:\\Windows\\System32\\OpenSSH\\ssh.exe");
    std::wstring fullCmd = QuoteWinArg(sshExe) + L" " + BuildSshBaseArgs(host, true, false) +
                           QuoteWinArg(host.TargetSpec()) + L" " + QuoteWinArg(remoteScript);

    DWORD exitCode = 1;
    std::wstring out = RunSilentProcessOutput(fullCmd, 8000, &exitCode);
    if (exitCode != 0) {
        std::wstring lower = ToLowerCopy(out);
        if (lower.find(L"permission denied") != std::wstring::npos ||
            lower.find(L"publickey") != std::wstring::npos ||
            lower.find(L"password") != std::wstring::npos ||
            lower.find(L"keyboard-interactive") != std::wstring::npos) {
            outError = L"SSH key authentication required for non-interactive file browsing. Use 'Connect SSH Terminal' (supports password prompt) or 'Setup SSH Key'.";
        } else if (lower.find(L"timed out") != std::wstring::npos || lower.find(L"could not resolve") != std::wstring::npos) {
            outError = L"Could not reach host: " + TrimWS(out);
        } else {
            outError = out.empty() ? L"Remote SSH command failed." : TrimWS(out);
        }
        return entries;
    }

    std::wstringstream ss(out);
    std::wstring line;
    std::wstring resolvedBase;
    while (std::getline(ss, line)) {
        line = TrimWS(line);
        if (line.empty()) continue;
        if (line.rfind(L"__PHIDE_PWD__:", 0) == 0) {
            resolvedBase = line.substr(14);
            if (!resolvedBase.empty()) outResolvedPath = resolvedBase;
            continue;
        }
        if (line == L"." || line == L".." || line == L"./" || line == L"../") continue;

        bool isDir = false;
        if (line.back() == L'/') {
            isDir = true;
            line.pop_back();
        }
        if (line.empty()) continue;

        RemoteFileEntry e{};
        e.name = line;
        e.isDir = isDir;
        e.depth = depth;
        std::wstring base = outResolvedPath;
        if (!base.empty() && base.back() != L'/') base += L'/';
        e.fullPath = base + line;
        entries.push_back(std::move(e));
    }

    std::sort(entries.begin(), entries.end(), [](const RemoteFileEntry& a, const RemoteFileEntry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return entries;
}

bool DownloadRemoteSshFile(
    const SshHostEntry& host,
    const std::wstring& remotePath,
    const std::wstring& localCachePath,
    std::wstring& outError
) {
    outError.clear();
    if (!HasScpClientTool()) {
        outError = L"OpenSSH scp.exe not found.";
        return false;
    }

    std::error_code ec;
    std::filesystem::path p(localCachePath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::wstring scpExe = ResolveExePath(L"scp.exe", L"C:\\Windows\\System32\\OpenSSH\\scp.exe");
    std::wstring remoteSpec = host.TargetSpec() + L":" + remotePath;
    std::wstring fullCmd = QuoteWinArg(scpExe) + L" " + BuildSshBaseArgs(host, true, true) +
                           QuoteWinArg(remoteSpec) + L" " + QuoteWinArg(localCachePath);

    DWORD exitCode = 1;
    std::wstring out = RunSilentProcessOutput(fullCmd, 12000, &exitCode);
    if (exitCode != 0 || !std::filesystem::exists(localCachePath, ec)) {
        outError = out.empty() ? L"Failed to download remote file via scp." : TrimWS(out);
        return false;
    }
    return true;
}

bool UploadRemoteSshFile(
    const SshHostEntry& host,
    const std::wstring& localCachePath,
    const std::wstring& remotePath,
    std::wstring& outError
) {
    outError.clear();
    if (!HasScpClientTool()) {
        outError = L"OpenSSH scp.exe not found.";
        return false;
    }

    std::wstring scpExe = ResolveExePath(L"scp.exe", L"C:\\Windows\\System32\\OpenSSH\\scp.exe");
    std::wstring remoteSpec = host.TargetSpec() + L":" + remotePath;
    std::wstring fullCmd = QuoteWinArg(scpExe) + L" " + BuildSshBaseArgs(host, true, true) +
                           QuoteWinArg(localCachePath) + L" " + QuoteWinArg(remoteSpec);

    DWORD exitCode = 1;
    std::wstring out = RunSilentProcessOutput(fullCmd, 12000, &exitCode);
    if (exitCode != 0) {
        outError = out.empty() ? L"Failed to upload file back to remote SSH host." : TrimWS(out);
        return false;
    }
    return true;
}

} // namespace remote

// =============================================================================
// RemoteWindowModal Implementation
// =============================================================================

void RemoteWindowModal::Open(RemoteModalMode initialMode, bool sshCurrentWindow) {
    m_isOpen = true;
    m_mode = initialMode;
    m_sshCurrentWindow = sshCurrentWindow;
    m_inputText.clear();
    m_statusBanner.clear();
    m_statusIsError = false;
    m_selectedIdx = 0;
    m_hoveredIdx = -1;
    m_scrollOffset = 0;

    // Immediately load SSH config hosts from disk
    m_sshHosts = remote::LoadSshConfigHosts();
    RebuildRows();

    // Refresh WSL & Docker asynchronously so sub-modes stay accurate without UI lag
    if (initialMode == RemoteModalMode::MainMenu ||
        initialMode == RemoteModalMode::ContainerPicker ||
        initialMode == RemoteModalMode::WslDistroPicker) {
        TriggerAsyncTargetRefresh();
    }
}

void RemoteWindowModal::OpenForSshBrowse(const remote::SshHostEntry& host) {
    m_isOpen = true;
    m_mode = RemoteModalMode::RemotePathInput;
    m_sshCurrentWindow = true;
    m_pendingSshHost = host;
    m_inputText = L"~";
    m_statusBanner.clear();
    m_statusIsError = false;
    m_selectedIdx = 0;
    m_hoveredIdx = -1;
    m_scrollOffset = 0;
    RebuildRows();
}

void RemoteWindowModal::Close() noexcept {
    m_isOpen = false;
    m_hoveredIdx = -1;
    m_hoverBackBtn = false;
}

void RemoteWindowModal::SetCachedTargets(
    std::vector<remote::SshHostEntry> sshHosts,
    std::vector<remote::WslDistroEntry> wslDistros,
    std::wstring wslStatus,
    std::vector<remote::DevContainerEntry> containers,
    std::wstring containerStatus
) {
    m_sshHosts = std::move(sshHosts);
    m_wslDistros = std::move(wslDistros);
    m_wslStatus = std::move(wslStatus);
    m_containers = std::move(containers);
    m_containerStatus = std::move(containerStatus);
    if (m_isOpen) {
        RebuildRows();
    }
}

void RemoteWindowModal::SetInputTextForTest(const std::wstring& text) {
    m_inputText = text;
    RebuildRows();
}

void RemoteWindowModal::TriggerAsyncTargetRefresh() {
    auto cbRepaint = m_callbacks.onRequestRepaint;
    auto cbUpdated = m_callbacks.onTargetsUpdated;
    std::thread([cbRepaint, cbUpdated]() {
        // Trigger parent cache refresh if wired; Window owns canonical cache
        if (cbUpdated) cbUpdated();
        if (cbRepaint) cbRepaint();
    }).detach();
}

void RemoteWindowModal::RebuildRows() {
    m_rows.clear();
    std::wstring filter = remote::ToLowerCopy(remote::TrimWS(m_inputText));
    auto matches = [&](const std::wstring& a, const std::wstring& b = L"") {
        if (filter.empty()) return true;
        return remote::ToLowerCopy(a).find(filter) != std::wstring::npos ||
               (!b.empty() && remote::ToLowerCopy(b).find(filter) != std::wstring::npos);
    };

    if (m_mode == RemoteModalMode::MainMenu) {
        struct MenuItemDef { const wchar_t* id; const wchar_t* label; const wchar_t* tag; int group; };
        const MenuItemDef defs[] = {
            { L"ssh_connect",       L"Connect to SSH Host...",                    L"Remote-SSH",     0 },
            { L"ssh_current",       L"Connect to SSH Host in Current Window...",  L"",               0 },
            { L"dc_open_folder",    L"Open Folder in Container",                  L"Dev Containers", 1 },
            { L"dc_attach",         L"Attach to Running Container",               L"",               1 },
            { L"dc_reopen",         L"Reopen in Container",                       L"",               1 },
            { L"wsl_connect",       L"Connect to WSL",                            L"Remote-WSL",     2 },
            { L"wsl_distro",        L"Connect to WSL using Distro...",            L"",               2 },
        };
        for (const auto& d : defs) {
            if (matches(d.label, d.tag)) {
                RemotePopupRow r{};
                r.id = d.id;
                r.label = d.label;
                r.categoryTag = d.tag;
                r.group = d.group;
                m_rows.push_back(std::move(r));
            }
        }
        // Assign category tag dynamically if first visible item in a group was filtered out
        if (!m_rows.empty() && m_rows[0].categoryTag.empty()) {
            // Keep clean even when filtered
        }
    } else if (m_mode == RemoteModalMode::SshHostPicker) {
        std::wstring raw = remote::TrimWS(m_inputText);
        remote::SshHostEntry adhoc{};
        if (!raw.empty() && remote::ParseAdhocSshInput(raw, adhoc)) {
            RemotePopupRow direct{};
            direct.id = L"ssh_direct";
            direct.label = (m_sshCurrentWindow ? L"Browse SSH Host: " : L"Connect SSH Terminal: ") + adhoc.DisplayDetail();
            direct.categoryTag = L"Direct";
            direct.group = 0;
            direct.isAccentAction = true;
            m_rows.push_back(std::move(direct));
        }

        for (size_t i = 0; i < m_sshHosts.size(); ++i) {
            const auto& h = m_sshHosts[i];
            std::wstring detail = h.DisplayDetail();
            if (matches(h.alias, detail)) {
                RemotePopupRow r{};
                r.id = L"ssh_host_" + std::to_wstring(i);
                r.label = h.alias + (detail != h.alias ? (L"   (" + detail + L")") : L"");
                r.categoryTag = m_sshCurrentWindow ? L"Browse Files" : L"SSH";
                r.group = 1;
                m_rows.push_back(std::move(r));
            }
        }

        if (m_rows.empty() && filter.empty()) {
            RemotePopupRow info{};
            info.id = L"ssh_none_info";
            info.label = L"No SSH hosts in ~/.ssh/config yet — add one or type user@host above";
            info.categoryTag = L"Empty";
            info.group = 1;
            m_rows.push_back(std::move(info));
        }

        RemotePopupRow addRow{};
        addRow.id = L"ssh_add_new";
        addRow.label = L"+ Add New SSH Host...";
        addRow.categoryTag = L"Configure";
        addRow.group = 2;
        addRow.isAccentAction = true;
        m_rows.push_back(std::move(addRow));

        RemotePopupRow cfgRow{};
        cfgRow.id = L"ssh_open_cfg";
        cfgRow.label = L"Open SSH Configuration File (~/.ssh/config)";
        cfgRow.categoryTag = L"Configure";
        cfgRow.group = 2;
        m_rows.push_back(std::move(cfgRow));

        RemotePopupRow keyRow{};
        keyRow.id = L"ssh_setup_key";
        keyRow.label = remote::HasSshKeyPair()
            ? L"SSH Key Auth Setup (Key Ready: ~/.ssh/id_ed25519)"
            : L"Generate SSH Key (Ed25519) for Passwordless Remote Browsing";
        keyRow.categoryTag = remote::HasSshKeyPair() ? L"Key Ready" : L"Setup Key";
        keyRow.group = 2;
        m_rows.push_back(std::move(keyRow));
    } else if (m_mode == RemoteModalMode::AddSshHost) {
        RemotePopupRow saveRow{};
        saveRow.id = L"save_ssh_host";
        std::wstring preview = remote::TrimWS(m_inputText);
        saveRow.label = preview.empty()
            ? L"Type SSH host above (e.g. ubuntu@192.168.1.50 or ssh user@host -p 2222)"
            : (L"Save Host & Connect: " + preview);
        saveRow.categoryTag = L"Enter";
        saveRow.group = 0;
        saveRow.isAccentAction = true;
        m_rows.push_back(std::move(saveRow));

        RemotePopupRow backRow{};
        backRow.id = L"back_to_ssh";
        backRow.label = L"Back to SSH Host List";
        backRow.categoryTag = L"Esc";
        backRow.group = 1;
        m_rows.push_back(std::move(backRow));
    } else if (m_mode == RemoteModalMode::RemotePathInput) {
        std::wstring pathVal = remote::TrimWS(m_inputText);
        if (pathVal.empty()) pathVal = L"~";
        RemotePopupRow openRow{};
        openRow.id = L"browse_entered_path";
        openRow.label = L"Browse Remote Folder \"" + pathVal + L"\" on " + m_pendingSshHost.TargetSpec();
        openRow.categoryTag = L"SFTP / SSH";
        openRow.group = 0;
        openRow.isAccentAction = true;
        m_rows.push_back(std::move(openRow));

        const wchar_t* presets[] = { L"~", L"/", L"/var/www", L"/home", L"/etc" };
        for (const auto* p : presets) {
            if (p != pathVal) {
                RemotePopupRow r{};
                r.id = std::wstring(L"browse_preset_") + p;
                r.label = std::wstring(L"Browse ") + p;
                r.categoryTag = L"Path";
                r.group = 1;
                m_rows.push_back(std::move(r));
            }
        }

        RemotePopupRow termRow{};
        termRow.id = L"open_ssh_terminal_fallback";
        termRow.label = L"Open Interactive SSH Terminal on " + m_pendingSshHost.TargetSpec();
        termRow.categoryTag = L"ConPTY";
        termRow.group = 2;
        m_rows.push_back(std::move(termRow));
    } else if (m_mode == RemoteModalMode::ContainerPicker) {
        for (size_t i = 0; i < m_containers.size(); ++i) {
            const auto& c = m_containers[i];
            if (matches(c.name, c.image)) {
                RemotePopupRow r{};
                r.id = L"container_" + std::to_wstring(i);
                r.label = c.name + L"  (" + c.image + L")";
                r.categoryTag = c.isRunning ? L"Running" : L"Stopped";
                r.group = 0;
                m_rows.push_back(std::move(r));
            }
        }
        if (m_rows.empty()) {
            RemotePopupRow info{};
            info.id = L"container_status_info";
            info.label = m_containerStatus.empty() ? L"No Docker containers found." : m_containerStatus;
            info.categoryTag = L"Refresh";
            info.group = 0;
            m_rows.push_back(std::move(info));
        }

        RemotePopupRow cfgDc{};
        cfgDc.id = L"create_devcontainer";
        cfgDc.label = L"Open / Create .devcontainer/devcontainer.json in Workspace";
        cfgDc.categoryTag = L"Dev Containers";
        cfgDc.group = 1;
        m_rows.push_back(std::move(cfgDc));
    } else if (m_mode == RemoteModalMode::WslDistroPicker) {
        for (size_t i = 0; i < m_wslDistros.size(); ++i) {
            const auto& d = m_wslDistros[i];
            if (matches(d.name, d.state)) {
                RemotePopupRow r{};
                r.id = L"wsl_distro_" + std::to_wstring(i);
                r.label = d.name + (d.isDefault ? L" (Default)" : L"");
                r.categoryTag = L"WSL" + d.version + L" · " + d.state;
                r.group = 0;
                m_rows.push_back(std::move(r));
            }
        }
        if (m_rows.empty()) {
            RemotePopupRow info{};
            info.id = L"wsl_empty_info";
            info.label = m_wslStatus.empty() ? L"No WSL distributions installed on this machine." : m_wslStatus;
            info.categoryTag = L"Status";
            info.group = 0;
            m_rows.push_back(std::move(info));

            RemotePopupRow inst{};
            inst.id = L"wsl_install";
            inst.label = L"Install WSL (Runs 'wsl --install' in Terminal)";
            inst.categoryTag = L"Remote-WSL";
            inst.group = 1;
            inst.isAccentAction = true;
            m_rows.push_back(std::move(inst));
        }
    }

    if (m_rows.empty()) {
        m_selectedIdx = -1;
    } else {
        m_selectedIdx = (std::max)(0, (std::min)(m_selectedIdx, static_cast<int>(m_rows.size()) - 1));
    }
    m_scrollOffset = 0;
}

void RemoteWindowModal::EnsureTextFormats(const Theme& theme, const phide::app::AppConfig& config) {
    if (!theme.dwriteFactory) return;
    float s = config.dpiScale;
    if (m_fmtRowLabel && std::abs(m_lastDpiScale - s) < 0.01f) return;
    m_lastDpiScale = s;

    auto makeFmt = [&](float ptSize, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align, Microsoft::WRL::ComPtr<IDWriteTextFormat>& out) {
        out.Reset();
        theme.dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            ptSize * s, L"en-US", &out
        );
        if (out) {
            out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            out->SetTextAlignment(align);
            out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            Microsoft::WRL::ComPtr<IDWriteInlineObject> trimSign;
            theme.dwriteFactory->CreateEllipsisTrimmingSign(out.Get(), &trimSign);
            DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            out->SetTrimming(&trimming, trimSign.Get());
        }
    };

    makeFmt(13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtRowLabel);
    makeFmt(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, m_fmtRowTag);
    makeFmt(13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtInput);
    makeFmt(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, m_fmtSmall);
}

void RemoteWindowModal::Render(
    ID2D1RenderTarget* pRT,
    const Theme& theme,
    const phide::app::AppConfig& config,
    float clientWidth,
    float clientHeight
) {
    if (!m_isOpen || !pRT) return;
    EnsureTextFormats(theme, config);
    m_lastClientW = clientWidth;
    m_lastClientH = clientHeight;

    const float modalW = (std::min)(config.ScaleF(560.0f), (std::max)(config.ScaleF(320.0f), clientWidth - config.ScaleF(40.0f)));
    const float modalX = (clientWidth - modalW) * 0.5f;
    const float modalY = config.ScaleF(36.0f);

    const float pad = config.ScaleF(8.0f);
    const float inputH = config.ScaleF(32.0f);
    const float rowH = config.ScaleF(28.0f);
    const float sepH = config.ScaleF(7.0f);
    const bool showBackPill = (m_mode != RemoteModalMode::MainMenu);
    const bool showBanner = !m_statusBanner.empty();
    const float bannerH = showBanner ? config.ScaleF(24.0f) : 0.0f;

    const int maxRowsVisible = 10;
    const int totalRows = static_cast<int>(m_rows.size());
    const int visibleRows = (std::min)(totalRows, maxRowsVisible);
    if (m_scrollOffset > (std::max)(0, totalRows - visibleRows)) {
        m_scrollOffset = (std::max)(0, totalRows - visibleRows);
    }

    // Measure height dynamically including group separators
    float listH = 0.0f;
    int prevGroup = -1;
    for (int v = 0; v < visibleRows; ++v) {
        int idx = m_scrollOffset + v;
        if (v > 0 && m_rows[idx].group != prevGroup) {
            listH += sepH;
        }
        prevGroup = m_rows[idx].group;
        listH += rowH;
    }

    const float modalH = pad + inputH + config.ScaleF(6.0f) + bannerH + listH + pad;
    m_modalBounds = D2D1::RectF(modalX, modalY, modalX + modalW, modalY + modalH);

    // Subtle dark drop shadow
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.42f), &shadowBrush);
    if (shadowBrush) {
        D2D1_RECT_F sRect = D2D1::RectF(modalX + 2.0f, modalY + 6.0f, modalX + modalW + 2.0f, modalY + modalH + 10.0f);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(sRect, config.ScaleF(7.0f), config.ScaleF(7.0f)), shadowBrush.Get());
    }

    // Modal background surface matching Phide theme
    ID2D1Brush* bgBrush = theme.brushMenuBg ? theme.brushMenuBg.Get() : theme.brushDrawerBg.Get();
    ID2D1Brush* borderBrush = theme.brushMenuBorder ? theme.brushMenuBorder.Get() : theme.brushBorderSubtle.Get();
    D2D1_ROUNDED_RECT modalRrc = D2D1::RoundedRect(m_modalBounds, config.ScaleF(6.0f), config.ScaleF(6.0f));
    pRT->FillRoundedRectangle(modalRrc, bgBrush);
    pRT->DrawRoundedRectangle(modalRrc, borderBrush, 1.0f);

    // Clip all modal interior cleanly
    pRT->PushAxisAlignedClip(m_modalBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Input bar at top
    m_inputBounds = D2D1::RectF(modalX + pad, modalY + pad, modalX + modalW - pad, modalY + pad + inputH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inputFillBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.145f, 0.149f, 0.161f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f),
        &inputFillBrush);
    D2D1_ROUNDED_RECT inputRrc = D2D1::RoundedRect(m_inputBounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
    pRT->FillRoundedRectangle(inputRrc, inputFillBrush ? static_cast<ID2D1Brush*>(inputFillBrush.Get()) : bgBrush);
    pRT->DrawRoundedRectangle(inputRrc, theme.brushBrandAccent.Get(), 1.25f);

    float textLeft = m_inputBounds.left + config.ScaleF(8.0f);
    if (showBackPill) {
        float backW = config.ScaleF(54.0f);
        m_backBtnBounds = D2D1::RectF(
            m_inputBounds.left + config.ScaleF(4.0f),
            m_inputBounds.top + config.ScaleF(4.0f),
            m_inputBounds.left + config.ScaleF(4.0f) + backW,
            m_inputBounds.bottom - config.ScaleF(4.0f));
        pRT->FillRoundedRectangle(
            D2D1::RoundedRect(m_backBtnBounds, config.ScaleF(3.0f), config.ScaleF(3.0f)),
            m_hoverBackBtn ? theme.brushBrandAccent.Get() : theme.brushItemHover.Get());
        if (m_fmtSmall) {
            pRT->DrawText(L"← Back", 6, m_fmtSmall.Get(), m_backBtnBounds,
                          m_hoverBackBtn ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
        }
        textLeft = m_backBtnBounds.right + config.ScaleF(8.0f);
    } else {
        m_backBtnBounds = {};
    }

    D2D1_RECT_F textRect = D2D1::RectF(textLeft, m_inputBounds.top, m_inputBounds.right - config.ScaleF(8.0f), m_inputBounds.bottom);
    IDWriteTextFormat* inputFmt = m_fmtInput ? m_fmtInput.Get() : theme.fontBody.Get();
    pRT->PushAxisAlignedClip(textRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (m_inputText.empty()) {
        std::wstring placeholder = L"Select an option to open a Remote Window";
        if (m_mode == RemoteModalMode::SshHostPicker) {
            placeholder = m_sshCurrentWindow
                ? L"Pick SSH host or type user@host to browse files..."
                : L"Pick SSH host or type user@host to open terminal...";
        } else if (m_mode == RemoteModalMode::AddSshHost) {
            placeholder = L"Enter ssh user@host -p 22 (saved to ~/.ssh/config)";
        } else if (m_mode == RemoteModalMode::RemotePathInput) {
            placeholder = L"Enter remote folder path (default: ~)";
        } else if (m_mode == RemoteModalMode::ContainerPicker) {
            placeholder = L"Filter containers by name or image...";
        } else if (m_mode == RemoteModalMode::WslDistroPicker) {
            placeholder = L"Select a WSL distribution...";
        }
        pRT->DrawText(placeholder.c_str(), static_cast<UINT32>(placeholder.size()), inputFmt, textRect, theme.brushTextSecondary.Get());
        // Draw steady/blinking focus bar at start
        if (((GetTickCount64() / 500) % 2) == 0) {
            pRT->DrawLine(
                D2D1::Point2F(textRect.left + 1.0f, textRect.top + config.ScaleF(6.0f)),
                D2D1::Point2F(textRect.left + 1.0f, textRect.bottom - config.ScaleF(6.0f)),
                theme.brushTextPrimary.Get(), 1.2f);
        }
    } else {
        pRT->DrawText(m_inputText.c_str(), static_cast<UINT32>(m_inputText.size()), inputFmt, textRect, theme.brushTextPrimary.Get());
        if (theme.dwriteFactory && ((GetTickCount64() / 500) % 2) == 0) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    m_inputText.c_str(), static_cast<UINT32>(m_inputText.size()), inputFmt,
                    textRect.right - textRect.left, textRect.bottom - textRect.top, &layout))) {
                DWRITE_TEXT_METRICS tm{};
                layout->GetMetrics(&tm);
                float caretX = (std::min)(textRect.right - 2.0f, textRect.left + tm.widthIncludingTrailingWhitespace + 1.0f);
                pRT->DrawLine(
                    D2D1::Point2F(caretX, textRect.top + config.ScaleF(6.0f)),
                    D2D1::Point2F(caretX, textRect.bottom - config.ScaleF(6.0f)),
                    theme.brushTextPrimary.Get(), 1.2f);
            }
        }
    }
    pRT->PopAxisAlignedClip();

    float curY = m_inputBounds.bottom + config.ScaleF(6.0f);

    if (showBanner) {
        D2D1_RECT_F bRect = D2D1::RectF(modalX + pad + config.ScaleF(6.0f), curY, modalX + modalW - pad - config.ScaleF(6.0f), curY + bannerH - config.ScaleF(2.0f));
        IDWriteTextFormat* tagFmt = m_fmtRowLabel ? m_fmtRowLabel.Get() : theme.fontMenuItem.Get();
        ID2D1Brush* bannerBrush = m_statusIsError ? theme.brushCommentAlert.Get() : theme.brushRunGreen.Get();
        pRT->PushAxisAlignedClip(bRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(m_statusBanner.c_str(), static_cast<UINT32>(m_statusBanner.size()), tagFmt, bRect, bannerBrush);
        pRT->PopAxisAlignedClip();
        curY += bannerH;
    }

    // Accent tag brush for unselected rows (#3794FF style)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> skyTagBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.216f, 0.580f, 1.000f, 1.0f) : D2D1::ColorF(0.00f, 0.42f, 0.80f, 1.0f),
        &skyTagBrush);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selRowBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.035f, 0.278f, 0.443f, 1.0f) : D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f),
        &selRowBrush);

    // Clear offscreen row bounds
    for (auto& r : m_rows) r.bounds = {};

    prevGroup = -1;
    for (int v = 0; v < visibleRows; ++v) {
        int idx = m_scrollOffset + v;
        auto& row = m_rows[idx];
        if (v > 0 && row.group != prevGroup) {
            float lineY = curY + sepH * 0.5f;
            pRT->DrawLine(
                D2D1::Point2F(modalX + pad, lineY),
                D2D1::Point2F(modalX + modalW - pad, lineY),
                theme.brushMenuSeparator ? theme.brushMenuSeparator.Get() : theme.brushBorderSubtle.Get(),
                1.0f);
            curY += sepH;
        }
        prevGroup = row.group;

        row.bounds = D2D1::RectF(modalX + pad, curY, modalX + modalW - pad, curY + rowH);
        const bool isSelected = (idx == m_selectedIdx);
        const bool isHovered = (idx == m_hoveredIdx);

        if (isSelected) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(row.bounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
            pRT->FillRoundedRectangle(rrc, selRowBrush ? static_cast<ID2D1Brush*>(selRowBrush.Get()) : theme.brushTreeSelected.Get());
        } else if (isHovered) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(row.bounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }

        // Strict horizontal non-overlapping split between left label and right category tag
        const float tagAreaW = row.categoryTag.empty() ? 0.0f : config.ScaleF(118.0f);
        D2D1_RECT_F rcLabel = D2D1::RectF(
            row.bounds.left + config.ScaleF(10.0f), row.bounds.top,
            row.bounds.right - tagAreaW - config.ScaleF(10.0f), row.bounds.bottom);
        D2D1_RECT_F rcTag = D2D1::RectF(
            row.bounds.right - tagAreaW, row.bounds.top,
            row.bounds.right - config.ScaleF(10.0f), row.bounds.bottom);

        IDWriteTextFormat* lblFmt = m_fmtRowLabel ? m_fmtRowLabel.Get() : theme.fontMenuItem.Get();
        ID2D1Brush* lblBrush = isSelected
            ? theme.brushTextWhite.Get()
            : (row.isAccentAction ? (skyTagBrush ? skyTagBrush.Get() : theme.brushBrandAccent.Get()) : theme.brushTextPrimary.Get());

        pRT->PushAxisAlignedClip(rcLabel, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(row.label.c_str(), static_cast<UINT32>(row.label.size()), lblFmt, rcLabel, lblBrush);
        pRT->PopAxisAlignedClip();

        if (!row.categoryTag.empty()) {
            IDWriteTextFormat* tagFmt = m_fmtRowTag ? m_fmtRowTag.Get() : theme.fontMenuShortcut.Get();
            ID2D1Brush* tagBrush = isSelected
                ? theme.brushTextWhite.Get()
                : (skyTagBrush ? skyTagBrush.Get() : theme.brushBrandAccent.Get());
            pRT->PushAxisAlignedClip(rcTag, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(row.categoryTag.c_str(), static_cast<UINT32>(row.categoryTag.size()), tagFmt, rcTag, tagBrush);
            pRT->PopAxisAlignedClip();
        }

        curY += rowH;
    }

    pRT->PopAxisAlignedClip();
}

bool RemoteWindowModal::HitTestModal(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_modalBounds.left && pt.x <= m_modalBounds.right &&
           pt.y >= m_modalBounds.top && pt.y <= m_modalBounds.bottom;
}

bool RemoteWindowModal::HitTestInput(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    if (m_backBtnBounds.right > m_backBtnBounds.left &&
        pt.x >= m_backBtnBounds.left && pt.x <= m_backBtnBounds.right &&
        pt.y >= m_backBtnBounds.top && pt.y <= m_backBtnBounds.bottom) {
        return false;
    }
    return pt.x >= m_inputBounds.left && pt.x <= m_inputBounds.right &&
           pt.y >= m_inputBounds.top && pt.y <= m_inputBounds.bottom;
}

bool RemoteWindowModal::HitTestInteractive(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    if (m_backBtnBounds.right > m_backBtnBounds.left &&
        pt.x >= m_backBtnBounds.left && pt.x <= m_backBtnBounds.right &&
        pt.y >= m_backBtnBounds.top && pt.y <= m_backBtnBounds.bottom) {
        return true;
    }
    for (const auto& r : m_rows) {
        if (r.bounds.right > r.bounds.left &&
            pt.x >= r.bounds.left && pt.x <= r.bounds.right &&
            pt.y >= r.bounds.top && pt.y <= r.bounds.bottom) {
            return true;
        }
    }
    return false;
}

void RemoteWindowModal::HandleMouseMove(POINT pt) {
    if (!m_isOpen) return;
    m_hoveredIdx = -1;
    m_hoverBackBtn = false;

    if (m_backBtnBounds.right > m_backBtnBounds.left &&
        pt.x >= m_backBtnBounds.left && pt.x <= m_backBtnBounds.right &&
        pt.y >= m_backBtnBounds.top && pt.y <= m_backBtnBounds.bottom) {
        m_hoverBackBtn = true;
        return;
    }

    for (size_t i = 0; i < m_rows.size(); ++i) {
        const auto& b = m_rows[i].bounds;
        if (b.right > b.left && pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            m_hoveredIdx = static_cast<int>(i);
            break;
        }
    }
}

bool RemoteWindowModal::HandleLButtonDown(POINT pt) {
    if (!m_isOpen) return false;
    if (!HitTestModal(pt)) {
        Close();
        return false;
    }

    if (m_backBtnBounds.right > m_backBtnBounds.left &&
        pt.x >= m_backBtnBounds.left && pt.x <= m_backBtnBounds.right &&
        pt.y >= m_backBtnBounds.top && pt.y <= m_backBtnBounds.bottom) {
        if (m_mode == RemoteModalMode::AddSshHost || m_mode == RemoteModalMode::RemotePathInput) {
            m_mode = RemoteModalMode::SshHostPicker;
        } else {
            m_mode = RemoteModalMode::MainMenu;
        }
        m_inputText.clear();
        m_statusBanner.clear();
        m_selectedIdx = 0;
        RebuildRows();
        return true;
    }

    for (size_t i = 0; i < m_rows.size(); ++i) {
        const auto& b = m_rows[i].bounds;
        if (b.right > b.left && pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            m_selectedIdx = static_cast<int>(i);
            ExecuteRow(static_cast<int>(i));
            return true;
        }
    }
    return true;
}

bool RemoteWindowModal::HandleMouseWheel(short delta) {
    if (!m_isOpen) return false;
    const int totalRows = static_cast<int>(m_rows.size());
    if (totalRows <= 10) return true;
    int maxScroll = totalRows - 10;
    if (delta < 0 && m_scrollOffset < maxScroll) m_scrollOffset++;
    else if (delta > 0 && m_scrollOffset > 0) m_scrollOffset--;
    return true;
}

bool RemoteWindowModal::HandleChar(wchar_t ch) {
    if (!m_isOpen) return false;
    if (ch == L'\r' || ch == L'\n') {
        if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_rows.size())) {
            ExecuteRow(m_selectedIdx);
        }
        return true;
    }
    if (ch == L'\b') {
        if (!m_inputText.empty()) {
            m_inputText.pop_back();
            m_statusBanner.clear();
            RebuildRows();
        }
        return true;
    }
    if (ch >= 32) {
        m_inputText += ch;
        m_statusBanner.clear();
        RebuildRows();
        return true;
    }
    return true;
}

bool RemoteWindowModal::HandleKeyDown(UINT vk, bool /*isCtrl*/, bool /*isShift*/) {
    if (!m_isOpen) return false;
    if (vk == VK_ESCAPE) {
        if (m_mode == RemoteModalMode::AddSshHost || m_mode == RemoteModalMode::RemotePathInput) {
            m_mode = RemoteModalMode::SshHostPicker;
            m_inputText.clear();
            m_statusBanner.clear();
            RebuildRows();
        } else if (m_mode != RemoteModalMode::MainMenu) {
            m_mode = RemoteModalMode::MainMenu;
            m_inputText.clear();
            m_statusBanner.clear();
            RebuildRows();
        } else {
            Close();
        }
        return true;
    }
    if (vk == VK_UP) {
        MoveSelection(-1);
        return true;
    }
    if (vk == VK_DOWN) {
        MoveSelection(1);
        return true;
    }
    if (vk == VK_RETURN) {
        if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_rows.size())) {
            ExecuteRow(m_selectedIdx);
        }
        return true;
    }
    if (vk == VK_BACK || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END) {
        return true; // keep keys inside modal
    }
    return true;
}

void RemoteWindowModal::MoveSelection(int delta) {
    if (m_rows.empty()) return;
    int n = static_cast<int>(m_rows.size());
    m_selectedIdx = (m_selectedIdx + delta + n) % n;
    if (m_selectedIdx < m_scrollOffset) m_scrollOffset = m_selectedIdx;
    else if (m_selectedIdx >= m_scrollOffset + 10) m_scrollOffset = m_selectedIdx - 9;
}

void RemoteWindowModal::ExecuteRow(int rowIdx) {
    if (rowIdx < 0 || rowIdx >= static_cast<int>(m_rows.size())) return;
    const std::wstring id = m_rows[rowIdx].id;

    if (id == L"ssh_connect" || id == L"ssh_current") {
        m_sshCurrentWindow = (id == L"ssh_current");
        m_mode = RemoteModalMode::SshHostPicker;
        m_inputText.clear();
        m_statusBanner.clear();
        m_sshHosts = remote::LoadSshConfigHosts();
        m_selectedIdx = 0;
        RebuildRows();
        return;
    }
    if (id == L"dc_open_folder" || id == L"dc_reopen") {
        Close();
        if (m_callbacks.onOpenOrCreateDevContainer) m_callbacks.onOpenOrCreateDevContainer();
        return;
    }
    if (id == L"dc_attach") {
        m_mode = RemoteModalMode::ContainerPicker;
        m_inputText.clear();
        m_statusBanner.clear();
        m_selectedIdx = 0;
        RebuildRows();
        TriggerAsyncTargetRefresh();
        return;
    }
    if (id == L"wsl_connect") {
        std::wstring distro;
        if (!m_wslDistros.empty()) {
            distro = m_wslDistros.front().name;
            for (const auto& d : m_wslDistros) {
                if (d.isDefault) { distro = d.name; break; }
            }
        }
        Close();
        if (!distro.empty()) {
            if (m_callbacks.onConnectWsl) m_callbacks.onConnectWsl(distro, false);
        } else {
            // If zero distros or unknown, open distro picker so user sees exact real status + Install option
            Open(RemoteModalMode::WslDistroPicker, false);
        }
        return;
    }
    if (id == L"wsl_distro") {
        m_mode = RemoteModalMode::WslDistroPicker;
        m_inputText.clear();
        m_statusBanner.clear();
        m_selectedIdx = 0;
        RebuildRows();
        TriggerAsyncTargetRefresh();
        return;
    }

    if (id == L"ssh_add_new") {
        m_mode = RemoteModalMode::AddSshHost;
        m_inputText.clear();
        m_statusBanner.clear();
        m_selectedIdx = 0;
        RebuildRows();
        return;
    }
    if (id == L"ssh_none_info") {
        m_mode = RemoteModalMode::AddSshHost;
        m_inputText.clear();
        m_statusBanner.clear();
        RebuildRows();
        return;
    }
    if (id == L"ssh_open_cfg") {
        Close();
        if (m_callbacks.onOpenSshConfig) m_callbacks.onOpenSshConfig();
        return;
    }
    if (id == L"ssh_setup_key") {
        Close();
        if (m_callbacks.onSetupSshKey) m_callbacks.onSetupSshKey();
        return;
    }
    if (id == L"back_to_ssh") {
        m_mode = RemoteModalMode::SshHostPicker;
        m_inputText.clear();
        m_statusBanner.clear();
        RebuildRows();
        return;
    }
    if (id == L"save_ssh_host") {
        remote::SshHostEntry saved{};
        std::wstring err;
        if (!remote::SaveSshConfigHost(m_inputText, saved, err)) {
            m_statusBanner = err;
            m_statusIsError = true;
            return;
        }
        m_sshHosts = remote::LoadSshConfigHosts();
        if (m_callbacks.onTargetsUpdated) m_callbacks.onTargetsUpdated();
        if (m_sshCurrentWindow) {
            OpenForSshBrowse(saved);
        } else {
            Close();
            if (m_callbacks.onConnectSshTerminal) m_callbacks.onConnectSshTerminal(saved, L"");
        }
        return;
    }
    if (id == L"ssh_direct") {
        remote::SshHostEntry host{};
        if (remote::ParseAdhocSshInput(m_inputText, host)) {
            if (m_sshCurrentWindow) {
                OpenForSshBrowse(host);
            } else {
                Close();
                if (m_callbacks.onConnectSshTerminal) m_callbacks.onConnectSshTerminal(host, L"");
            }
        }
        return;
    }
    if (id.rfind(L"ssh_host_", 0) == 0) {
        size_t idx = 0;
        try { idx = static_cast<size_t>(std::stoul(id.substr(9))); } catch (...) { return; }
        if (idx < m_sshHosts.size()) {
            const auto host = m_sshHosts[idx];
            if (m_sshCurrentWindow) {
                OpenForSshBrowse(host);
            } else {
                Close();
                if (m_callbacks.onConnectSshTerminal) m_callbacks.onConnectSshTerminal(host, L"");
            }
        }
        return;
    }

    if (id == L"browse_entered_path") {
        std::wstring p = remote::TrimWS(m_inputText);
        if (p.empty()) p = L"~";
        auto host = m_pendingSshHost;
        Close();
        if (m_callbacks.onBrowseSshFolder) m_callbacks.onBrowseSshFolder(host, p);
        return;
    }
    if (id.rfind(L"browse_preset_", 0) == 0) {
        std::wstring p = id.substr(14);
        auto host = m_pendingSshHost;
        Close();
        if (m_callbacks.onBrowseSshFolder) m_callbacks.onBrowseSshFolder(host, p);
        return;
    }
    if (id == L"open_ssh_terminal_fallback") {
        auto host = m_pendingSshHost;
        std::wstring dir = remote::TrimWS(m_inputText);
        Close();
        if (m_callbacks.onConnectSshTerminal) m_callbacks.onConnectSshTerminal(host, dir);
        return;
    }

    if (id.rfind(L"container_", 0) == 0 && id != L"container_status_info") {
        size_t idx = 0;
        try { idx = static_cast<size_t>(std::stoul(id.substr(10))); } catch (...) { return; }
        if (idx < m_containers.size()) {
            auto c = m_containers[idx];
            Close();
            if (m_callbacks.onAttachContainer) m_callbacks.onAttachContainer(c);
        }
        return;
    }
    if (id == L"container_status_info") {
        TriggerAsyncTargetRefresh();
        return;
    }
    if (id == L"create_devcontainer") {
        Close();
        if (m_callbacks.onOpenOrCreateDevContainer) m_callbacks.onOpenOrCreateDevContainer();
        return;
    }

    if (id.rfind(L"wsl_distro_", 0) == 0) {
        size_t idx = 0;
        try { idx = static_cast<size_t>(std::stoul(id.substr(11))); } catch (...) { return; }
        if (idx < m_wslDistros.size()) {
            std::wstring name = m_wslDistros[idx].name;
            Close();
            if (m_callbacks.onConnectWsl) m_callbacks.onConnectWsl(name, false);
        }
        return;
    }
    if (id == L"wsl_install") {
        Close();
        if (m_callbacks.onInstallWsl) m_callbacks.onInstallWsl();
        return;
    }
}

} // namespace phide::ui
