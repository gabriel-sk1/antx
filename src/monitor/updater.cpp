#include "updater.h"
#include "config.h"
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#pragma comment(lib, "wininet.lib")

std::string DownloadString(const std::wstring& url) {
    std::string response;
    HINTERNET hInternet = InternetOpenW(L"AntxAutoUpdater/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return response;

    const wchar_t* headers = L"User-Agent: AntxAutoUpdater/1.0\r\nAccept: application/vnd.github.v3+json\r\n";
    HINTERNET hConnect = InternetOpenUrlW(hInternet, url.c_str(), headers, -1, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);

    if (hConnect) {
        char buffer[4096];
        DWORD bytesRead;
        while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response.append(buffer, bytesRead);
        }
        InternetCloseHandle(hConnect);
    }
    InternetCloseHandle(hInternet);
    return response;
}

bool DownloadBinaryFile(const std::wstring& url, const std::wstring& destPath) {
    HINTERNET hInternet = InternetOpenW(L"AntxAutoUpdater/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }

    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead;
    DWORD bytesWritten;
    bool success = true;

    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
            success = false;
            break;
        }
    }

    CloseHandle(hFile);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return success;
}

bool IsNewerVersionAvailable(const std::string& latestTag, const std::string& currentVersion) {
    if (latestTag.empty() || currentVersion.empty()) return false;
    if (latestTag != currentVersion) return true;
    return false;
}

void AutoUpdaterLoop() {
    std::wstring apiUrl = L"https://api.github.com/repos/bielsk1/antx/releases/latest";

    std::this_thread::sleep_for(std::chrono::minutes(1));

    while (true) {
        try {
            std::string json = DownloadString(apiUrl);

            size_t tagPos = json.find("\"tag_name\"");
            if (tagPos != std::string::npos) {
                size_t startQuote = json.find("\"", tagPos + 10);
                if (startQuote != std::string::npos) {
                    size_t endQuote = json.find("\"", startQuote + 1);
                    if (endQuote != std::string::npos) {
                        std::string latestTag = json.substr(startQuote + 1, endQuote - startQuote - 1);

                        if (IsNewerVersionAvailable(latestTag, ANTX_VERSION)) {
                            std::string searchToken = "\"browser_download_url\"";
                            size_t urlPos = json.find(searchToken);
                            while (urlPos != std::string::npos) {
                                size_t uStartQuote = json.find("\"", urlPos + searchToken.length());
                                if (uStartQuote != std::string::npos) {
                                    size_t uEndQuote = json.find("\"", uStartQuote + 1);
                                    if (uEndQuote != std::string::npos) {
                                        std::string downloadUrlStr = json.substr(uStartQuote + 1, uEndQuote - uStartQuote - 1);
                                        if (downloadUrlStr.find(".exe") != std::string::npos) {
                                            std::wstring downloadUrl(downloadUrlStr.begin(), downloadUrlStr.end());

                                            wchar_t tempPath[MAX_PATH];
                                            GetTempPathW(MAX_PATH, tempPath);
                                            std::wstring installerPath = std::wstring(tempPath) + L"Antx_Setup_Update.exe";

                                            if (DownloadBinaryFile(downloadUrl, installerPath)) {
                                                ShellExecuteW(NULL, L"open", installerPath.c_str(), L"/SILENT", NULL, SW_HIDE);
                                                ExitProcess(0);
                                            }
                                            break;
                                        }
                                    }
                                }
                                urlPos = json.find(searchToken, urlPos + 1);
                            }
                        }
                    }
                }
            }
        } catch (...) {
        }

        std::this_thread::sleep_for(std::chrono::hours(12));
    }
}

void StartAutoUpdaterThread() {
    std::thread updaterThread(AutoUpdaterLoop);
    updaterThread.detach();
}
