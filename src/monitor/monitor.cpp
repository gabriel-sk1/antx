#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <psapi.h>
#include "config.h"
#include "updater.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ABRIR 1001
#define ID_TRAY_SAIR 1002

struct StreakData {
    long long startTime;
    long long lastResetTime;
    int resetCount;
    bool rameleiFlag;
};

StreakData g_streakData;
NOTIFYICONDATAW g_nid = { sizeof(g_nid) };
HWND g_hTrayWnd = nullptr;

void LoadStreakData(StreakData& data);
void SaveStreakData(const StreakData& data);
void ResetStreak(StreakData& data);
bool RegisterAutostart();
void LaunchDashboardApp();
std::string GetDashboardHtmlContent();
void HttpServerThread();
void WindowScannerLoop();

void CreateDataDirectory() {
    CreateDirectoryW(antx::STREAK_DATA_DIR_W.c_str(), nullptr);
}

void LoadStreakData(StreakData& data) {
    CreateDataDirectory();
    std::ifstream inFile(antx::STREAK_DATA_FILE, std::ios::binary);
    if (inFile.is_open()) {
        inFile.read((char*)&data, sizeof(StreakData));
        inFile.close();
    } else {
        auto now = std::chrono::system_clock::now();
        data.startTime = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        data.lastResetTime = 0;
        data.resetCount = 0;
        data.rameleiFlag = false;
        SaveStreakData(data);
    }
}

void SaveStreakData(const StreakData& data) {
    CreateDataDirectory();
    std::ofstream outFile(antx::STREAK_DATA_FILE, std::ios::binary | std::ios::trunc);
    if (outFile.is_open()) {
        outFile.write((const char*)&data, sizeof(StreakData));
        outFile.close();
    }
}

void ResetStreak(StreakData& data) {
    if (data.rameleiFlag) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    long long nowSecs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    data.lastResetTime = nowSecs;
    data.startTime = nowSecs;
    data.resetCount++;
    data.rameleiFlag = true;
    SaveStreakData(data);

    std::wstring historyFile = antx::STREAK_DATA_DIR_W + L"\\history.log";
    std::ofstream outHistory(historyFile.c_str(), std::ios::app);
    if (outHistory.is_open()) {
        outHistory << nowSecs << "\n";
        outHistory.close();
    }
    system("icacls \"C:\\ProgramData\\Antx\\history.log\" /grant *S-1-1-0:F /Q >nul 2>nul");

    LaunchDashboardApp();
}

bool RegisterAutostart() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, antx::AUTOSTART_REG_PATH, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegSetValueExW(hKey, antx::AUTOSTART_REG_VALUE, 0, REG_SZ, 
                       (const BYTE*)path.c_str(), (DWORD)(path.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void LaunchDashboardApp() {
    wchar_t cmdLine[] = L"\"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" --app=http://127.0.0.1:48927/";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:48927/", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

bool StringContainsIgnoreCaseW(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return false;
    if (haystack.empty()) return false;

    std::wstring hay = haystack;
    std::wstring ndl = needle;

    for (auto& c : hay) c = ::towlower(c);
    for (auto& c : ndl) c = ::towlower(c);

    return hay.find(ndl) != std::wstring::npos;
}

std::wstring GetProcessNameW(DWORD processID) {
    wchar_t szProcessPath[MAX_PATH] = L"";
    DWORD dwSize = MAX_PATH;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);
    if (hProcess) {
        if (QueryFullProcessImageNameW(hProcess, 0, szProcessPath, &dwSize)) {
            std::wstring fullPath(szProcessPath);
            size_t lastSlash = fullPath.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) {
                CloseHandle(hProcess);
                return fullPath.substr(lastSlash + 1);
            }
            CloseHandle(hProcess);
            return fullPath;
        }
        CloseHandle(hProcess);
    }
    return L"<unknown>";
}

BOOL CALLBACK EnumMonitorWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t titleBuf[512] = {0};
    int length = GetWindowTextW(hwnd, titleBuf, 512);
    if (length == 0) return TRUE;
    std::wstring windowTitle(titleBuf);

    DWORD processID;
    GetWindowThreadProcessId(hwnd, &processID);
    std::wstring procName = GetProcessNameW(processID);

    bool isMonitoredBrowser = false;
    for (const auto& browser : antx::MONITORED_BROWSERS) {
        if (StringContainsIgnoreCaseW(procName, browser)) {
            isMonitoredBrowser = true;
            break;
        }
    }

    if (isMonitoredBrowser) {
        std::wstring debugPath = antx::STREAK_DATA_DIR_W + L"\\monitor_debug.log";
        std::wofstream debugLog(debugPath.c_str(), std::ios::app);
        if (debugLog.is_open()) {
            debugLog << L"[" << processID << L"] proc: " << procName << L" | title: " << windowTitle << std::endl;
            debugLog.close();
        }

        for (const auto& keyword : antx::DEFAULT_TRIGGER_KEYWORDS) {
            if (StringContainsIgnoreCaseW(windowTitle, keyword)) {
                std::wofstream triggerLog(debugPath.c_str(), std::ios::app);
                if (triggerLog.is_open()) {
                    triggerLog << L"*** GATILHO ACIONADO! Keyword: " << keyword << L" ***" << std::endl;
                    triggerLog.close();
                }

                PostMessage(hwnd, WM_CLOSE, 0, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processID);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }

                LoadStreakData(g_streakData);
                ResetStreak(g_streakData);
                break;
            }
        }
    }
    return TRUE;
}

void WindowScannerLoop() {
    while (true) {
        EnumWindows(EnumMonitorWindowsProc, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK || lp == WM_LBUTTONUP) {
            LaunchDashboardApp();
        }
        else if (lp == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABRIR, L"Abrir Painel de Vitória");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SAIR, L"Sair");

            SetMenuDefaultItem(hMenu, ID_TRAY_ABRIR, FALSE);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_TRAY_ABRIR) {
            LaunchDashboardApp();
        }
        else if (LOWORD(wp) == ID_TRAY_SAIR) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return 0;
}

void HttpServerThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    char opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(48927);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSock);
        WSACleanup();
        return;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSock);
        WSACleanup();
        return;
    }

    while (true) {
        SOCKET clientSock = accept(listenSock, nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) {
            break;
        }

        std::vector<char> reqBuf(4096, 0);
        int bytesRead = recv(clientSock, reqBuf.data(), (int)reqBuf.size() - 1, 0);
        if (bytesRead > 0) {
            std::string req(reqBuf.data());

            if (req.find("GET /api/streak") != std::string::npos) {
                LoadStreakData(g_streakData);

                std::stringstream json;
                json << "{\"startTime\":" << g_streakData.startTime 
                     << ",\"resetCount\":" << g_streakData.resetCount 
                     << ",\"rameleiFlag\":" << (g_streakData.rameleiFlag ? "true" : "false") << "}";
                std::string body = json.str();

                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
                send(clientSock, resp.str().c_str(), (int)resp.str().length(), 0);
            }
            else if (req.find("POST /api/recomecar") != std::string::npos) {
                LoadStreakData(g_streakData);
                g_streakData.rameleiFlag = false;
                SaveStreakData(g_streakData);

                std::string body = "{\"success\":true}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
                send(clientSock, resp.str().c_str(), (int)resp.str().length(), 0);
            }
            else if (req.find("GET /api/history") != std::string::npos) {
                std::wstring historyFile = antx::STREAK_DATA_DIR_W + L"\\history.log";
                std::ifstream inHistory(historyFile.c_str());
                std::string body = "[";
                std::string line;
                bool first = true;
                if (inHistory.is_open()) {
                    while (std::getline(inHistory, line)) {
                        if (!line.empty()) {
                            if (!first) body += ",";
                            body += line;
                            first = false;
                        }
                    }
                    inHistory.close();
                }
                body += "]";

                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
                send(clientSock, resp.str().c_str(), (int)resp.str().length(), 0);
            }
            else if (req.find("GET /api/routines") != std::string::npos) {
                std::wstring routinesFile = antx::STREAK_DATA_DIR_W + L"\\routines.json";
                std::ifstream inRoutines(routinesFile.c_str());
                std::string body = "[]";
                if (inRoutines.is_open()) {
                    std::stringstream buffer;
                    buffer << inRoutines.rdbuf();
                    body = buffer.str();
                    if (body.empty()) body = "[]";
                    inRoutines.close();
                }

                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
                send(clientSock, resp.str().c_str(), (int)resp.str().length(), 0);
            }
            else if (req.find("POST /api/routines") != std::string::npos) {
                size_t bodyPos = req.find("\r\n\r\n");
                if (bodyPos != std::string::npos) {
                    std::string requestBody = req.substr(bodyPos + 4);
                    std::wstring routinesFile = antx::STREAK_DATA_DIR_W + L"\\routines.json";
                    std::ofstream outRoutines(routinesFile.c_str(), std::ios::trunc);
                    if (outRoutines.is_open()) {
                        outRoutines << requestBody;
                        outRoutines.close();
                    }
                }
                std::string body = "{\"success\":true}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
                send(clientSock, resp.str().c_str(), (int)resp.str().length(), 0);
            }
            else if (req.find("GET / ") != std::string::npos || req.find("GET /index.html") != std::string::npos) {
                std::string body = GetDashboardHtmlContent();
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html; charset=utf-8\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
                send(clientSock, resp.str().c_str(), (int)resp.str().length(), 0);
            } else {
                std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(clientSock, resp.c_str(), (int)resp.length(), 0);
            }
        }
        closesocket(clientSock);
    }
    closesocket(listenSock);
    WSACleanup();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\AntxMonitorMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LaunchDashboardApp();
        return 0;
    }

    RegisterAutostart();
    LoadStreakData(g_streakData);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AntxTrayWindowClass";
    RegisterClassW(&wc);

    g_hTrayWnd = CreateWindowExW(0, L"AntxTrayWindowClass", L"Antx Tray Helper", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!g_hTrayWnd) {
        return 0;
    }

    g_nid.hWnd = g_hTrayWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, (LPCWSTR)32518);
    wcscpy_s(g_nid.szTip, L"Antx - Vitória Ativa");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    std::thread httpThread(HttpServerThread);
    httpThread.detach();

    std::thread scannerThread(WindowScannerLoop);
    scannerThread.detach();

    LaunchDashboardApp();

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return 0;
}

std::string GetDashboardHtmlContent() {
    return R"HTML(<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <title>Antx Dashboard</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@400;500;600;800&family=JetBrains+Mono:wght@700&display=swap" rel="stylesheet">
    <style>
        body { font-family: 'Outfit', sans-serif; }
        .mono { font-family: 'JetBrains Mono', monospace; }
        .neon-glow { text-shadow: 0 0 15px rgba(16, 185, 129, 0.4); }
        .neon-box { box-shadow: 0 0 35px rgba(16, 185, 129, 0.08); }
    </style>
</head>
<body class="bg-slate-950 text-slate-100 min-h-screen flex items-center justify-center p-6 selection:bg-emerald-500 selection:text-slate-950 relative overflow-hidden">

    <!-- Efeitos de iluminação de fundo premium -->
    <div class="fixed w-[600px] h-[600px] rounded-full bg-emerald-500/5 blur-[150px] -top-32 -left-32 pointer-events-none"></div>
    <div class="fixed w-[600px] h-[600px] rounded-full bg-cyan-500/5 blur-[150px] -bottom-32 -right-32 pointer-events-none"></div>

    <main class="w-full max-w-4xl relative z-10 flex flex-col gap-6">

        <!-- Header minimalista -->
        <div class="flex items-center justify-between px-2 mb-2">
            <div class="flex items-center gap-3">
                <div class="w-10 h-10 rounded-xl bg-emerald-500/10 border border-emerald-500/20 flex items-center justify-center text-emerald-400">
                    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="w-5 h-5"><path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75m-3-7.036A11.959 11.959 0 013.598 6 11.99 11.99 0 003 9.749c0 5.592 3.824 10.29 9 11.622 5.176-1.332 9-6.03 9-11.622 0-1.31-.21-2.57-.598-3.751h-.152c-3.196 0-6.1-1.248-8.25-3.285z" /></svg>
                </div>
                <div>
                    <h1 class="text-xl font-extrabold tracking-tight text-white">Antx Blocker</h1>
                    <p class="text-[10px] text-emerald-500/80 tracking-widest uppercase font-bold">Foco Blindado</p>
                </div>
            </div>
            <div class="bg-slate-900/80 border border-slate-800 px-4 py-2 rounded-xl flex items-center gap-3">
                <span class="w-2 h-2 rounded-full bg-emerald-500 shadow-[0_0_8px_rgba(16,185,129,0.8)]"></span>
                <span class="text-[10px] font-bold text-slate-300 uppercase tracking-widest">Proteção Ativa</span>
            </div>
        </div>

        <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
            <!-- Card do Timer (Ocupa 2 colunas) -->
            <div class="md:col-span-2 bg-slate-900/60 border border-slate-800/80 rounded-3xl p-8 neon-box backdrop-blur-md">
                <p class="text-slate-300 text-sm font-semibold mb-6 tracking-wide uppercase">Tempo de Foco Contínuo</p>

                <div class="grid grid-cols-4 gap-4">
                    <div class="flex flex-col items-center">
                        <div class="w-full bg-emerald-950/40 border-2 border-emerald-500/30 rounded-2xl py-6 text-center shadow-lg shadow-emerald-500/5">
                            <span id="days" class="text-4xl md:text-5xl font-extrabold text-emerald-400 mono tracking-tight">00</span>
                        </div>
                        <span class="text-xs font-bold text-emerald-500/80 mt-3 uppercase tracking-widest">Dias</span>
                    </div>
                    <div class="flex flex-col items-center">
                        <div class="w-full bg-emerald-950/40 border-2 border-emerald-500/30 rounded-2xl py-6 text-center shadow-lg shadow-emerald-500/5">
                            <span id="hours" class="text-4xl md:text-5xl font-extrabold text-emerald-400 mono tracking-tight">00</span>
                        </div>
                        <span class="text-xs font-bold text-emerald-500/80 mt-3 uppercase tracking-widest">Horas</span>
                    </div>
                    <div class="flex flex-col items-center">
                        <div class="w-full bg-emerald-950/40 border-2 border-emerald-500/30 rounded-2xl py-6 text-center shadow-lg shadow-emerald-500/5">
                            <span id="minutes" class="text-4xl md:text-5xl font-extrabold text-emerald-400 mono tracking-tight">00</span>
                        </div>
                        <span class="text-xs font-bold text-emerald-500/80 mt-3 uppercase tracking-widest">Mins</span>
                    </div>
                    <div class="flex flex-col items-center">
                        <div class="w-full bg-emerald-950/40 border-2 border-emerald-500/30 rounded-2xl py-6 text-center shadow-lg shadow-emerald-500/5">
                            <span id="seconds" class="text-4xl md:text-5xl font-extrabold text-emerald-400 mono tracking-tight">00</span>
                        </div>
                        <span class="text-xs font-bold text-emerald-500/80 mt-3 uppercase tracking-widest">Segs</span>
                    </div>
                </div>
            </div>

            <!-- Card de Stats (Resets) -->
            <div class="bg-slate-900/60 border border-slate-800/80 rounded-3xl p-8 backdrop-blur-md flex flex-col justify-center items-center text-center">
                <div class="w-16 h-16 rounded-full bg-red-500/10 border border-red-500/20 flex items-center justify-center text-red-400 mb-4">
                    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="w-8 h-8"><path stroke-linecap="round" stroke-linejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" /></svg>
                </div>
                <h3 class="text-5xl font-black text-white mb-2" id="reset-counter-big">0</h3>
                <p class="text-slate-400 text-sm font-semibold uppercase tracking-wide">Tentativas Bloqueadas</p>
                <p class="text-xs text-slate-500 mt-4 leading-relaxed">Vezes que o Antx fechou a janela na sua cara.</p>
            </div>
        </div>
    </main>

    <!-- Tela agressiva de cobrança ("Você Rameleu") -->
    <div id="ramelei-overlay" class="fixed inset-0 bg-slate-950/98 backdrop-blur-md z-50 flex items-center justify-center p-6 hidden">
        <div class="w-full max-w-lg bg-red-950/40 border border-red-500/30 rounded-3xl p-10 text-center shadow-[0_0_80px_rgba(239,68,68,0.15)] relative overflow-hidden">
            <div class="absolute inset-0 bg-[url('data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSI4IiBoZWlnaHQ9IjgiPgo8cmVjdCB3aWR0aD0iOCIgaGVpZ2h0PSI4IiBmaWxsPSIjZmZmIiBmaWxsLW9wYWNpdHk9IjAuMDIiLz4KPHBhdGggZD0iTTAgMEw4IDhaTTAgOEw4IDBaIiBzdHJva2U9IiNmZmYiIHN0cm9rZS1vcGFjaXR5PSIwLjAyIiBzdHJva2Utd2lkdGg9IjEiLz4KPC9zdmc+')] opacity-50"></div>
            <div class="relative z-10">
                <div class="w-20 h-20 rounded-full bg-red-500/10 border border-red-500/30 flex items-center justify-center text-red-500 mx-auto mb-6"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="w-10 h-10"><path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126zM12 15.75h.007v.008H12v-.008z" /></svg></div>
                <h2 class="text-4xl font-black text-red-500 tracking-tight mb-4 uppercase drop-shadow-[0_0_15px_rgba(239,68,68,0.5)]">VOCÊ RAMELEU NA MISSÃO!</h2>
                <p class="text-slate-300 text-base font-medium leading-relaxed mb-10">O Antx interceptou uma tentativa de acesso bloqueada.<br>O seu contador de vitória foi <b class="text-red-400">zerado</b> e a tentativa foi registrada.<br><br>Não se sabote. Levante a cabeça e recomece AGORA!</p>
                <button onclick="recomecarJornada();" class="w-full py-5 bg-red-600 hover:bg-red-500 text-white font-black rounded-2xl shadow-[0_0_20px_rgba(239,68,68,0.4)] transition-all duration-300 transform active:scale-95 text-base tracking-widest uppercase">Assumir Falha e Recomeçar</button>
            </div>
        </div>
    </div>

    <script>
        let startTime = 0;
        let rameleiActive = false;

        async function fetchStreak() {
            try {
                const response = await fetch('/api/streak');
                const data = await response.json();
                startTime = data.startTime;
                document.getElementById('reset-counter-big').innerText = data.resetCount;
                if (data.rameleiFlag && !rameleiActive) {
                    rameleiActive = true;
                    document.getElementById('ramelei-overlay').classList.remove('hidden');
                }
            } catch (err) { console.error(err); }
        }

        async function recomecarJornada() {
            try {
                await fetch('/api/recomecar', { method: 'POST' });
                rameleiActive = false;
                document.getElementById('ramelei-overlay').classList.add('hidden');
                fetchStreak();
            } catch (err) { console.error(err); }
        }

        function updateTimer() {
            if (startTime === 0) return;
            const now = Math.floor(Date.now() / 1000);
            let diff = now - startTime;
            if (diff < 0) diff = 0;
            const days = Math.floor(diff / 86400); diff %= 86400;
            const hours = Math.floor(diff / 3600); diff %= 3600;
            const minutes = Math.floor(diff / 60); const seconds = diff % 60;
            document.getElementById('days').innerText = String(days).padStart(2, '0');
            document.getElementById('hours').innerText = String(hours).padStart(2, '0');
            document.getElementById('minutes').innerText = String(minutes).padStart(2, '0');
            document.getElementById('seconds').innerText = String(seconds).padStart(2, '0');
        }

        fetchStreak();
        setInterval(fetchStreak, 1500);
        setInterval(updateTimer, 1000);
    </script>
</body>
</html>)HTML";
}


