#include "app/window.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wrl.h>
#include <WebView2.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <map>
#include <set>
#include <thread>

#include "app/monitor.h"
#include "app/notify.h"
#include "app/settings.h"
#include "core/text.h"
#include "engine/engine.h"
#include "engine/pipe.h"
#include "engine/service.h"
#include "engine/winutil.h"

#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::Callback;
using nlohmann::json;

namespace why::app {
namespace {

constexpr UINT kPostToPage = WM_APP + 1;
constexpr UINT kShowAlert = WM_APP + 2;
constexpr UINT kNotifyIcon = WM_APP + 3;
constexpr UINT kCommandOpen = 1;
constexpr UINT kCommandExit = 2;
constexpr const wchar_t* kOrigin = L"https://app.why/";
constexpr const wchar_t* kClassName = L"WHYWindow";
constexpr std::uintmax_t kMaxReportBytes = 64 * 1024 * 1024;
constexpr int kResourceUi = 101;

// Where the state comes from: the installed monitor, or an engine that lives as long as this window.
class Backend {
public:
    virtual ~Backend() = default;
    virtual std::string state() = 0;
    virtual std::uint64_t waitForChange(std::uint64_t known) = 0;
    virtual bool caseAction(const std::string& key, const std::string& action) = 0;
    // The monitor's own tray notifier already announces what the monitor finds.
    virtual bool notifies() const { return false; }
};

class RemoteBackend final : public Backend {
public:
    std::string state() override { return pipeRequest(R"({"cmd":"state"})").value_or("{}"); }
    std::uint64_t waitForChange(std::uint64_t known) override {
        const auto reply = pipeRequest(json{{"cmd", "wait"}, {"rev", known}}.dump());
        if (!reply) {
            Sleep(3000);
            return known;
        }
        const auto parsed = json::parse(*reply, nullptr, false);
        return parsed.is_object() ? parsed.value("rev", known) : known;
    }
    bool caseAction(const std::string& key, const std::string& action) override {
        const auto reply = pipeRequest(json{{"cmd", "case"}, {"key", key}, {"action", action}}.dump());
        if (!reply) return false;
        const auto parsed = json::parse(*reply, nullptr, false);
        return parsed.is_object() && parsed.value("ok", false);
    }
};

class LocalBackend final : public Backend {
public:
    LocalBackend() : engine_(options()) { engine_.start(); }
    std::string state() override { return *engine_.state(); }
    std::uint64_t waitForChange(std::uint64_t known) override { return engine_.waitForChange(known, 3000); }
    bool caseAction(const std::string& key, const std::string& action) override { return engine_.applyCaseAction(key, action); }
    bool notifies() const override { return true; }

private:
    // History stays in memory, but "I tried a fix" is the person's word and is kept in their own folder.
    static EngineOptions options() {
        EngineOptions options;
        wil::unique_cotaskmem_string folder;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
            options.overridesPath = std::wstring(folder.get()) + L"\\WHY\\case-fixes.jsonl";
        }
        return options;
    }

    Engine engine_;
};

std::shared_ptr<Backend> connectBackend() {
    if (pipeRequest(R"({"cmd":"wait","rev":0})").has_value()) return std::make_shared<RemoteBackend>();
    return std::make_shared<LocalBackend>();
}

bool systemUsesDarkTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

std::wstring chooseFile(HWND owner, bool save) {
    wchar_t path[MAX_PATH] = L"";
    if (save) wcscpy_s(path, L"WHY-report.whyreport.json");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"WHY? report\0*.whyreport.json\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    return ok ? path : L"";
}

// The Windows folder picker. Empty when the person cancelled.
std::wstring chooseFolder(HWND owner) {
    auto dialog = wil::CoCreateInstanceNoThrow<IFileOpenDialog>(CLSID_FileOpenDialog);
    if (!dialog) return L"";
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose the folder WHY? should focus on");
    if (FAILED(dialog->Show(owner))) return L"";
    wil::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return L"";
    wil::unique_cotaskmem_string path;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return L"";
    return path.get();
}

std::wstring chooseProgram(HWND owner) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Programs\0*.exe\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Choose the program WHY? should focus on";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&dialog) ? path : L"";
}

// Programs running now, outside the Windows folder, so the person can pick the one they are building.
json runningPrograms() {
    std::wstring windows(MAX_PATH, L'\0');
    windows.resize(GetWindowsDirectoryW(windows.data(), MAX_PATH));
    const std::string windowsDir = toLower(toUtf8(windows)) + "\\";

    std::set<std::string> seen;
    json list = json::array();
    wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) return list;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry)) {
        wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process) continue;
        wchar_t image[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (!QueryFullProcessImageNameW(process.get(), 0, image, &size)) continue;
        const std::string path = toUtf8(image);
        if (toLower(path).starts_with(windowsDir) || !seen.insert(toLower(path)).second) continue;
        list.push_back({{"name", toUtf8(entry.szExeFile)}, {"path", path}});
    }
    std::sort(list.begin(), list.end(), [](const json& a, const json& b) {
        return toLower(a.value("name", "")) < toLower(b.value("name", ""));
    });
    return list;
}

// A report may come from anywhere, so it is checked before the interface sees it.
std::optional<std::string> loadReport(const std::wstring& path, std::string& error) {
    std::error_code code;
    const auto size = std::filesystem::file_size(path, code);
    if (code || size > kMaxReportBytes) {
        error = "This file is too large or could not be read.";
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = json::parse(buffer.str(), nullptr, false);
    if (!parsed.is_object() || parsed.value("schema", 0) != 1 || !parsed.contains("cases") || !parsed["cases"].is_array()) {
        error = "This file is not a WHY? report.";
        return std::nullopt;
    }
    parsed["mode"] = "report";
    parsed["scanning"] = false;
    return parsed.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool openWindowsTool(const std::string& tool) {
    const wchar_t* file = nullptr;
    const wchar_t* args = nullptr;
    if (tool == "device_manager") file = L"devmgmt.msc";
    else if (tool == "services") file = L"services.msc";
    else if (tool == "reliability_monitor") { file = L"perfmon.exe"; args = L"/rel"; }
    else if (tool == "memory_diagnostic") file = L"mdsched.exe";
    else if (tool == "windows_security") file = L"windowsdefender://threat";
    else return false;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file, args, nullptr, SW_SHOWNORMAL)) > 32;
}

// Runs this executable elevated with the given arguments and waits for it. False if the person declined.
bool runElevatedAndWait(const std::wstring& arguments, DWORD& exitCode) {
    const std::wstring self = executablePath();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return false;
    wil::unique_handle process(info.hProcess);
    WaitForSingleObject(process.get(), INFINITE);
    GetExitCodeProcess(process.get(), &exitCode);
    return true;
}

class AppWindow {
public:
    AppWindow(HINSTANCE instance, const std::optional<std::wstring>& reportPath, wil::unique_mutex_nothrow single, bool background)
        : instance_(instance), single_(std::move(single)), startupReport_(reportPath), background_(background) {}

    int run() {
        createWindow();
        startWebView();
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        running_ = false;
        controller_.reset();
        webview_.reset();
        environment_.reset();
        removeUserData();
        // The watcher may be blocked waiting on the monitor. The process exit ends it.
        ExitProcess(0);
    }

private:
    void createWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &AppWindow::windowProc;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = CreateSolidBrush(systemUsesDarkTheme() ? RGB(15, 24, 36) : RGB(238, 242, 247));
        windowClass.lpszClassName = kClassName;
        RegisterClassExW(&windowClass);

        window_ = CreateWindowExW(0, kClassName, toWide(WHY_RELEASE_NAME).c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1240, 820, nullptr, nullptr, instance_, this);
        THROW_LAST_ERROR_IF_NULL(window_);
        const BOOL dark = systemUsesDarkTheme();
        DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        // Mica, like the Windows 11 built-in tools. Older builds reject the attribute and keep the solid background.
        constexpr DWORD kSystemBackdropType = 38;  // DWMWA_SYSTEMBACKDROP_TYPE
        constexpr int kMainWindowBackdrop = 2;     // DWMSBT_MAINWINDOW
        if (SUCCEEDED(DwmSetWindowAttribute(window_, kSystemBackdropType, &kMainWindowBackdrop, sizeof(kMainWindowBackdrop)))) {
            const MARGINS whole{-1, -1, -1, -1};
            DwmExtendFrameIntoClientArea(window_, &whole);
            // Black is what DWM treats as see-through in an extended frame.
            SetClassLongPtrW(window_, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(GetStockObject(BLACK_BRUSH)));
            mica_ = true;
        }
        if (background_) showNotifyIcon();
        else ShowWindow(window_, SW_SHOWNORMAL);
    }

    void showNotifyIcon() {
        if (notifyIcon_) return;
        addNotifyIcon(window_, kNotifyIcon, L"WHY? is watching this PC");
        notifyIcon_ = true;
    }

    void bringToFront() {
        ShowWindow(window_, SW_SHOW);
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
    }

    // Closing hides the window when the person chose to keep WHY? watching in the background.
    bool hideInsteadOfClosing() {
        if (quitting_ || !loadSettings().keepInBackground) return false;
        ShowWindow(window_, SW_HIDE);
        if (!notifyIcon_) {
            showNotifyIcon();
            showAlert(window_, {L"WHY? keeps watching in the background",
                                L"It will tell you when something goes wrong. Right-click its icon to exit.", false, true});
        }
        return true;
    }

    void startWebView() {
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        // WebView2 needs a folder for its own data. It is private to this run and deleted on exit.
        userData_ = std::wstring(temp) + L"WHY-WebView2-" + std::to_wstring(GetCurrentProcessId());

        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userData_.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) return reportMissingWebView();
                environment_ = env;
                env->CreateCoreWebView2Controller(
                    window_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                 [this](HRESULT created, ICoreWebView2Controller* controller) -> HRESULT {
                                     if (FAILED(created)) return reportMissingWebView();
                                     onControllerReady(controller);
                                     return S_OK;
                                 }).Get());
                return S_OK;
            }).Get());
        if (FAILED(hr)) reportMissingWebView();
    }

    HRESULT reportMissingWebView() {
        MessageBoxW(window_,
                    L"WHY? needs the Microsoft Edge WebView2 Runtime to show its window, and it is not available on this PC.\n\n"
                    L"You can still collect a report from a command prompt with:\n    why collect\n\n"
                    L"Then open the report with WHY? on another PC.",
                    L"WHY?", MB_ICONWARNING);
        PostQuitMessage(1);
        return S_OK;
    }

    void onControllerReady(ICoreWebView2Controller* controller) {
        controller_ = controller;
        controller_->get_CoreWebView2(webview_.put());

        wil::com_ptr<ICoreWebView2Settings> settings;
        webview_->get_Settings(settings.put());
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsWebMessageEnabled(TRUE);
#ifdef NDEBUG
        settings->put_AreDevToolsEnabled(FALSE);
#endif

        // With Mica the page is see-through, so the backdrop shows wherever the interface leaves it.
        if (mica_) {
            if (const auto controller2 = controller_.try_query<ICoreWebView2Controller2>()) {
                controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
                webview_->AddScriptToExecuteOnDocumentCreated(L"document.documentElement.dataset.backdrop = 'mica';", nullptr);
            }
        }

        // The page is embedded in the executable, and nothing else may ever load.
        webview_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        EventRegistrationToken token{};
        webview_->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                wil::com_ptr<ICoreWebView2WebResourceRequest> request;
                args->get_Request(request.put());
                wil::unique_cotaskmem_string uri;
                request->get_Uri(&uri);
                wil::com_ptr<ICoreWebView2WebResourceResponse> response;
                if (_wcsicmp(uri.get(), kOrigin) == 0) {
                    environment_->CreateWebResourceResponse(uiStream().get(), 200, L"OK",
                                                            L"Content-Type: text/html; charset=utf-8\r\n"
                                                            L"Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; "
                                                            L"style-src 'unsafe-inline'; img-src data:; font-src data:",
                                                            response.put());
                } else {
                    environment_->CreateWebResourceResponse(nullptr, 403, L"Blocked", nullptr, response.put());
                }
                args->put_Response(response.get());
                return S_OK;
            }).Get(),
            &token);
        webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                wil::unique_cotaskmem_string uri;
                args->get_Uri(&uri);
                if (_wcsicmp(uri.get(), kOrigin) != 0) args->put_Cancel(TRUE);
                return S_OK;
            }).Get(),
            &token);
        webview_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(),
            &token);
        webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                wil::unique_cotaskmem_string text;
                if (SUCCEEDED(args->get_WebMessageAsJson(&text))) onMessage(toUtf8(text.get()));
                return S_OK;
            }).Get(),
            &token);

        resize();
        webview_->Navigate(kOrigin);
        startBackend();
    }

    wil::com_ptr<IStream> uiStream() {
        const HRSRC resource = FindResourceW(instance_, MAKEINTRESOURCEW(kResourceUi), MAKEINTRESOURCEW(10));
        const HGLOBAL loaded = resource ? LoadResource(instance_, resource) : nullptr;
        const void* bytes = loaded ? LockResource(loaded) : nullptr;
        wil::com_ptr<IStream> stream;
        if (bytes) stream.attach(SHCreateMemStream(static_cast<const BYTE*>(bytes), SizeofResource(instance_, resource)));
        return stream;
    }

    void startBackend() {
        if (startupReport_) {
            std::string error;
            if (auto report = loadReport(*startupReport_, error)) {
                const std::lock_guard lock(mutex_);
                report_ = std::move(*report);
            }
        }
        std::thread([this] {
            setBackend(connectBackend());
            pushEvent("stateChanged");
            std::uint64_t rev = 0;
            while (running_) {
                const auto backend = currentBackend();
                const auto next = backend->waitForChange(rev);
                if (next != rev) {
                    rev = next;
                    pushEvent("stateChanged");
                    if (backend->notifies()) notifyFresh(backend->state());
                }
            }
        }).detach();
    }

    void setBackend(std::shared_ptr<Backend> backend) {
        const std::lock_guard lock(mutex_);
        backend_ = std::move(backend);
    }

    std::shared_ptr<Backend> currentBackend() {
        const std::lock_guard lock(mutex_);
        return backend_;
    }

    // Problems that happen while the window is open get a banner. What was already on record is on the Home screen.
    void notifyFresh(const std::string& state) {
        auto alert = freshAlert(state, notifiedUpTo_, loadSettings());
        if (!alert) return;
        auto* owned = new Alert(std::move(*alert));
        if (!PostMessageW(window_, kShowAlert, 0, reinterpret_cast<LPARAM>(owned))) delete owned;
    }

    void pushEvent(const char* name) { postToPage(json{{"event", name}}.dump()); }

    // Any thread may call this. WebView2 is only touched on the window thread.
    void postToPage(std::string message) {
        auto* owned = new std::string(std::move(message));
        if (!PostMessageW(window_, kPostToPage, 0, reinterpret_cast<LPARAM>(owned))) delete owned;
    }

    void reply(std::int64_t id, const std::string& rawResult) {
        postToPage("{\"id\":" + std::to_string(id) + ",\"result\":" + rawResult + "}");
    }

    std::string currentState() {
        std::shared_ptr<Backend> backend;
        {
            const std::lock_guard lock(mutex_);
            if (!report_.empty()) return report_;
            backend = backend_;
        }
        return backend ? backend->state() : "{}";
    }

    void onMessage(const std::string& text) {
        const auto message = json::parse(text, nullptr, false);
        if (!message.is_object()) return;
        const auto id = message.value("id", std::int64_t{0});
        const std::string command = message.value("cmd", "");

        if (command == "getState") {
            std::thread([this, id] { reply(id, currentState()); }).detach();
        } else if (command == "caseAction") {
            const std::string key = message.value("key", "");
            const std::string action = message.value("action", "");
            std::thread([this, id, key, action] {
                const auto backend = currentBackend();
                const bool ok = backend && backend->caseAction(key, action);
                reply(id, json{{"ok", ok}}.dump());
            }).detach();
        } else if (command == "revealPath") {
            // Only something that exists, shown selected in File Explorer. Nothing is opened or run.
            const std::wstring path = toWide(message.value("path", ""));
            const DWORD attributes = GetFileAttributesW(path.c_str());
            bool ok = false;
            if (!path.empty() && attributes != INVALID_FILE_ATTRIBUTES) {
                const std::wstring arguments = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? L"\"" + path + L"\"" : L"/select,\"" + path + L"\"";
                ok = reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
            }
            reply(id, ok ? R"({"ok":true})" : R"({"ok":false,"error":"That file or folder is no longer there."})");
        } else if (command == "openLocalhost") {
            // A local dev server in the default browser. The page can only name a port, never a whole address.
            const int port = message.value("port", 0);
            bool ok = false;
            if (port > 0 && port < 65536) {
                const std::wstring url = L"http://localhost:" + std::to_wstring(port) + L"/";
                ok = reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
            }
            reply(id, json{{"ok", ok}}.dump());
        } else if (command == "openTool") {
            reply(id, json{{"ok", openWindowsTool(message.value("tool", ""))}}.dump());
        } else if (command == "saveReport") {
            saveReport(id);
        } else if (command == "openReport") {
            openReport(id);
        } else if (command == "closeReport") {
            {
                const std::lock_guard lock(mutex_);
                report_.clear();
            }
            reply(id, R"({"ok":true})");
            pushEvent("stateChanged");
        } else if (command == "getSettings") {
            reply(id, json{{"settings", toJson(loadSettings())}, {"elevated", isElevated()}}.dump());
        } else if (command == "saveSettings") {
            const Settings settings = settingsFromJson(message.value("settings", json::object()));
            saveSettings(settings);
            const std::string error = applyToWindows(settings);
            reply(id, error.empty() ? R"({"ok":true})" : json{{"ok", false}, {"error", error}}.dump());
        } else if (command == "chooseFolder") {
            const std::wstring path = chooseFolder(window_);
            reply(id, path.empty() ? R"({"ok":false,"cancelled":true})" : json{{"ok", true}, {"path", toUtf8(path)}}.dump());
        } else if (command == "chooseProgram") {
            const std::wstring path = chooseProgram(window_);
            reply(id, path.empty() ? R"({"ok":false,"cancelled":true})" : json{{"ok", true}, {"path", toUtf8(path)}}.dump());
        } else if (command == "listProcesses") {
            std::thread([this, id] { reply(id, processes_.sample().dump(-1, ' ', false, json::error_handler_t::replace)); }).detach();
        } else if (command == "systemStats") {
            std::thread([this, id] { reply(id, system_.sample().dump(-1, ' ', false, json::error_handler_t::replace)); }).detach();
        } else if (command == "programIcons") {
            std::vector<std::wstring> paths;
            if (message.contains("paths") && message["paths"].is_array()) {
                for (const auto& path : message["paths"]) {
                    if (path.is_string()) paths.push_back(toWide(path.get<std::string>()));
                }
            }
            std::thread([this, id, paths = std::move(paths)] {
                // The shell and WIC are COM, so this thread joins an apartment of its own.
                const auto com = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
                reply(id, programIcons(paths).dump(-1, ' ', false, json::error_handler_t::replace));
            }).detach();
        } else if (command == "listPrograms") {
            std::thread([this, id] { reply(id, runningPrograms().dump()); }).detach();
        } else if (command == "restartElevated") {
            restartElevated(id);
        } else if (command == "installLive") {
            changeLive(id, L"service install", true);
        } else if (command == "uninstallLive") {
            changeLive(id, L"service uninstall", false);
        } else {
            reply(id, R"({"ok":false})");
        }
    }

    void saveReport(std::int64_t id) {
        const std::wstring path = chooseFile(window_, true);
        if (path.empty()) return reply(id, R"({"ok":false,"cancelled":true})");
        std::thread([this, id, path] {
            std::ofstream file(path, std::ios::binary);
            file << makeReport(currentState());
            file.close();
            reply(id, json{{"ok", !file.fail()}, {"path", toUtf8(path)}}.dump());
        }).detach();
    }

    void openReport(std::int64_t id) {
        const std::wstring path = chooseFile(window_, false);
        if (path.empty()) return reply(id, R"({"ok":false,"cancelled":true})");
        std::string error;
        auto report = loadReport(path, error);
        if (!report) return reply(id, json{{"ok", false}, {"error", error}}.dump());
        {
            const std::lock_guard lock(mutex_);
            report_ = std::move(*report);
        }
        reply(id, R"({"ok":true})");
        pushEvent("stateChanged");
    }

    void restartElevated(std::int64_t id) {
        // The new instance would otherwise think this window is still open.
        single_.reset();
        const std::wstring self = executablePath();
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"runas", self.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return;
        }
        single_.try_create(L"Local\\WHY-Window");
        reply(id, R"({"ok":false,"cancelled":true})");
    }

    void changeLive(std::int64_t id, std::wstring arguments, bool installing) {
        std::thread([this, id, arguments = std::move(arguments), installing] {
            DWORD exitCode = 1;
            if (!runElevatedAndWait(arguments, exitCode)) return reply(id, R"({"ok":false,"cancelled":true})");
            if (exitCode != 0) return reply(id, R"({"ok":false,"error":"Windows could not complete the change."})");
            if (installing) {
                // The service needs a moment before it accepts connections.
                for (int i = 0; i < 20 && !pipeRequest(R"({"cmd":"wait","rev":0})"); ++i) Sleep(250);
            }
            setBackend(connectBackend());
            reply(id, R"({"ok":true})");
            pushEvent("stateChanged");
        }).detach();
    }

    void resize() {
        if (!controller_) return;
        RECT bounds{};
        GetClientRect(window_, &bounds);
        controller_->put_Bounds(bounds);
    }

    void removeUserData() {
        // WebView2's own processes need a moment to let go of the folder.
        for (int i = 0; i < 15; ++i) {
            std::error_code code;
            std::filesystem::remove_all(userData_, code);
            if (!code) return;
            Sleep(200);
        }
    }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        switch (message) {
            case WM_SIZE:
                if (self) self->resize();
                return 0;
            case WM_DPICHANGED: {
                const auto* rect = reinterpret_cast<RECT*>(lparam);
                SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case kPostToPage: {
                std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lparam));
                if (self && self->webview_) self->webview_->PostWebMessageAsJson(toWide(*text).c_str());
                return 0;
            }
            case kShowAlert: {
                std::unique_ptr<Alert> alert(reinterpret_cast<Alert*>(lparam));
                if (self) self->showNotifyIcon();
                showAlert(window, *alert);
                return 0;
            }
            case kNotifyIcon:
                if (!self) return 0;
                if (LOWORD(lparam) == NIN_BALLOONUSERCLICK || LOWORD(lparam) == WM_LBUTTONUP) {
                    self->bringToFront();
                } else if (LOWORD(lparam) == WM_RBUTTONUP) {
                    HMENU menu = CreatePopupMenu();
                    AppendMenuW(menu, MF_STRING, kCommandOpen, L"Open WHY?");
                    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit WHY?");
                    POINT point{};
                    GetCursorPos(&point);
                    SetForegroundWindow(window);
                    const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window, nullptr);
                    DestroyMenu(menu);
                    if (chosen == kCommandOpen) self->bringToFront();
                    if (chosen == kCommandExit) {
                        self->quitting_ = true;
                        DestroyWindow(window);
                    }
                }
                return 0;
            case WM_CLOSE:
                if (self && self->hideInsteadOfClosing()) return 0;
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                if (self && self->notifyIcon_) removeNotifyIcon(window);
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wparam, lparam);
        }
    }

    HINSTANCE instance_;
    wil::unique_mutex_nothrow single_;
    std::optional<std::wstring> startupReport_;
    HWND window_ = nullptr;
    bool mica_ = false;
    bool notifyIcon_ = false;
    bool background_ = false;
    bool quitting_ = false;
    std::int64_t notifiedUpTo_ = nowUnixMs();
    std::wstring userData_;
    wil::com_ptr<ICoreWebView2Environment> environment_;
    wil::com_ptr<ICoreWebView2Controller> controller_;
    wil::com_ptr<ICoreWebView2> webview_;
    std::mutex mutex_;
    std::shared_ptr<Backend> backend_;
    std::string report_;
    SystemMonitor system_;
    ProcessMonitor processes_;
    std::atomic<bool> running_{true};
};

}  // namespace

int runWindow(HINSTANCE instance, const std::optional<std::wstring>& reportPath, bool background) {
    wil::unique_mutex_nothrow single;
    bool alreadyRunning = false;
    if (single.try_create(L"Local\\WHY-Window", 0, MUTEX_ALL_ACCESS, nullptr, &alreadyRunning) && alreadyRunning) {
        // Started again at sign-in while already running: nothing to show.
        if (background) return 0;
        if (const HWND existing = FindWindowW(kClassName, nullptr)) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    // "Always run as administrator": ask Windows for it now, the ordinary way, with its confirmation prompt.
    // At sign-in WHY? starts without it rather than greeting the person with a prompt.
    if (!background && !reportPath && !isElevated() && loadSettings().runAsAdmin) {
        single.reset();
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"runas", executablePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32) return 0;
        // Declined: carry on with what can be read without it.
        single.try_create(L"Local\\WHY-Window");
    }

    const auto coInit = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
    AppWindow window(instance, reportPath, std::move(single), background);
    return window.run();
}

}  // namespace why::app
