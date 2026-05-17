#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>
#include <psapi.h>
#include "config.h"

SERVICE_STATUS          g_SvcStatus;
SERVICE_STATUS_HANDLE   g_SvcStatusHandle = nullptr;
HANDLE                  g_SvcStopEvent = nullptr;

VOID WINAPI SvcMain(DWORD dwArgc, LPWSTR* lpszArgv);
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl);
VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
void RunBlockerLoop();

bool StringContainsIgnoreCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](wchar_t ch1, wchar_t ch2) {
            return towlower(ch1) == towlower(ch2);
        }
    );
    return it != haystack.end();
}

bool StringContainsIgnoreCaseA(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) {
            return tolower(ch1) == tolower(ch2);
        }
    );
    return it != haystack.end();
}

bool IsUninstallAuthorized() {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::REGISTRY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, antx::REG_VAL_UNINSTALL_AUTH, nullptr, nullptr, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (value == 1);
        }
        RegCloseKey(hKey);
    }
    return false;
}

void FlushDNSCache() {
    typedef BOOL(WINAPI* pDnsFlushResolverCache)();
    HMODULE hDnsApi = LoadLibraryA("dnsapi.dll");
    if (hDnsApi) {
        pDnsFlushResolverCache flushFunc = (pDnsFlushResolverCache)GetProcAddress(hDnsApi, "DnsFlushResolverCache");
        if (flushFunc) {
            flushFunc();
        }
        FreeLibrary(hDnsApi);
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmd[] = "cmd.exe /c ipconfig /flushdns";
    if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void EnforceHostsFile() {
    std::ifstream inFile(antx::HOSTS_FILE_PATH);
    std::vector<std::string> lines;
    std::string line;
    bool inBlock = false;
    bool hasMarker = false;

    if (inFile.is_open()) {
        while (std::getline(inFile, line)) {
            if (line.find(antx::BLOCK_MARKER_START) != std::string::npos) {
                inBlock = true;
                hasMarker = true;
                continue;
            }
            if (line.find(antx::BLOCK_MARKER_END) != std::string::npos) {
                inBlock = false;
                continue;
            }
            if (!inBlock) {
                lines.push_back(line);
            }
        }
        inFile.close();
    }

    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }

    std::ofstream outFile(antx::HOSTS_FILE_PATH, std::ios::trunc);
    if (outFile.is_open()) {
        for (const auto& l : lines) {
            outFile << l << "\n";
        }

        outFile << "\n" << antx::BLOCK_MARKER_START << "\n";
        outFile << "# Protecao ativa antx. Nao altere este bloco.\n";
        for (const auto& domain : antx::DEFAULT_BLOCKED_DOMAINS) {
            outFile << "0.0.0.0 " << domain << "\n";
        }
        outFile << antx::BLOCK_MARKER_END << "\n";
        outFile.close();

        FlushDNSCache();
    }
}

std::wstring GetProcessName(DWORD processID) {
    wchar_t szProcessName[MAX_PATH] = L"<unknown>";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);
    if (hProcess) {
        HMODULE hMod;
        DWORD cbNeeded;
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseNameW(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(wchar_t));
        }
        CloseHandle(hProcess);
    }
    return std::wstring(szProcessName);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;

    int length = GetWindowTextLengthW(hwnd);
    if (length == 0) return TRUE;

    std::vector<wchar_t> titleBuf(length + 1);
    GetWindowTextW(hwnd, titleBuf.data(), length + 1);
    std::wstring windowTitle(titleBuf.data());

    DWORD processID;
    GetWindowThreadProcessId(hwnd, &processID);

    std::wstring procName = GetProcessName(processID);

    bool isMonitoredBrowser = false;
    for (const auto& browser : antx::MONITORED_BROWSERS) {
        if (StringContainsIgnoreCase(procName, browser)) {
            isMonitoredBrowser = true;
            break;
        }
    }

    if (isMonitoredBrowser) {
        for (const auto& keyword : antx::DEFAULT_TRIGGER_KEYWORDS) {
            if (StringContainsIgnoreCase(windowTitle, keyword)) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);

                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processID);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
                break;
            }
        }
    }

    return TRUE;
}

void EnforceBrowserPolicies() {
    for (int i = 0; i < antx::CHROMIUM_POLICY_COUNT; i++) {
        HKEY hKey;
        DWORD disposition;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, antx::CHROMIUM_POLICY_PATHS[i], 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disposition) == ERROR_SUCCESS) {
            const wchar_t* dohOff = L"off";
            RegSetValueExW(hKey, L"DnsOverHttpsMode", 0, REG_SZ,
                           (const BYTE*)dohOff, (DWORD)(wcslen(dohOff) + 1) * sizeof(wchar_t));
            RegCloseKey(hKey);
        }
    }

    {
        HKEY hKey;
        DWORD disposition;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, antx::FIREFOX_DOH_POLICY_PATH, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disposition) == ERROR_SUCCESS) {
            DWORD disabled = 0;
            DWORD locked = 1;
            RegSetValueExW(hKey, L"Enabled", 0, REG_DWORD, (const BYTE*)&disabled, sizeof(DWORD));
            RegSetValueExW(hKey, L"Locked", 0, REG_DWORD, (const BYTE*)&locked, sizeof(DWORD));
            RegCloseKey(hKey);
        }
    }

    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::WINDOWS_DNS_PARAMS_PATH, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            DWORD disabled = 0;
            RegSetValueExW(hKey, L"EnableAutoDoh", 0, REG_DWORD, (const BYTE*)&disabled, sizeof(DWORD));
            RegCloseKey(hKey);
        }
    }
}

void RunBlockerLoop() {
    int hostsCounter = 0;

    while (WaitForSingleObject(g_SvcStopEvent, 0) == WAIT_TIMEOUT) {
        EnumWindows(EnumWindowsProc, 0);

        hostsCounter++;
        if (hostsCounter >= 20) {
            EnforceHostsFile();
            EnforceBrowserPolicies();
            hostsCounter = 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc > 1 && wcscmp(argv[1], L"--debug") == 0) {
        std::wcout << L"Iniciando antx_service em modo Debug (Console)..." << std::endl;
        g_SvcStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        EnforceHostsFile();

        RunBlockerLoop();

        CloseHandle(g_SvcStopEvent);
        return 0;
    }

    SERVICE_TABLE_ENTRYW DispatchTable[] = {
        { const_cast<LPWSTR>(antx::SERVICE_NAME), (LPSERVICE_MAIN_FUNCTIONW)SvcMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(DispatchTable)) {
        std::cerr << "Erro ao iniciar o Dispatcher do Servico. Use '--debug' para rodar em modo console." << std::endl;
    }
    return 0;
}

VOID WINAPI SvcMain(DWORD dwArgc, LPWSTR* lpszArgv) {
    g_SvcStatusHandle = RegisterServiceCtrlHandlerW(
        antx::SERVICE_NAME,
        SvcCtrlHandler
    );

    if (!g_SvcStatusHandle) {
        return;
    }

    g_SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_SvcStatus.dwServiceSpecificExitCode = 0;

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    g_SvcStopEvent = CreateEvent(
        nullptr,
        TRUE,
        FALSE,
        nullptr
    );

    if (g_SvcStopEvent == nullptr) {
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    EnforceHostsFile();

    EnforceBrowserPolicies();

    RunBlockerLoop();

    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

VOID WINAPI SvcCtrlHandler(DWORD dwCtrl) {
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (IsUninstallAuthorized()) {
            ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
            SetEvent(g_SvcStopEvent);
        } else {
            ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
        }
        break;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }
}

VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    static DWORD dwCheckPoint = 1;

    g_SvcStatus.dwCurrentState = dwCurrentState;
    g_SvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    g_SvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING) {
        g_SvcStatus.dwControlsAccepted = 0;
    } else {
        g_SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) {
        g_SvcStatus.dwCheckPoint = 0;
    } else {
        g_SvcStatus.dwCheckPoint = dwCheckPoint++;
    }

    SetServiceStatus(g_SvcStatusHandle, &g_SvcStatus);
}
