#ifndef ANTX_CONFIG_H
#define ANTX_CONFIG_H

#include <vector>
#include <string>

namespace antx {

    #define ANTX_VERSION "v1.0.0"

    const wchar_t REGISTRY_PATH[] = L"SOFTWARE\\Antx";

    const wchar_t REG_VAL_HASH[] = L"PasswordHash";
    const wchar_t REG_VAL_SALT[] = L"PasswordSalt";
    const wchar_t REG_VAL_UNINSTALL_AUTH[] = L"UninstallAuthorized";

    const wchar_t SERVICE_NAME[] = L"AntxService";
    const wchar_t SERVICE_DISPLAY_NAME[] = L"Antx Blocker Service";
    const wchar_t SERVICE_DESCRIPTION[] = L"Servico em segundo plano para monitorar, bloquear gatilhos e proteger a integridade do arquivo hosts.";

    const std::string HOSTS_FILE_PATH = "C:\\Windows\\System32\\drivers\\etc\\hosts";

    const std::string BLOCK_MARKER_START = "# --- ANTX BLOCK SYSTEM START ---";
    const std::string BLOCK_MARKER_END = "# --- ANTX BLOCK SYSTEM END ---";

    const std::vector<std::string> DEFAULT_BLOCKED_DOMAINS = {
        "xvideos.com", "www.xvideos.com",
        "pornhub.com", "www.pornhub.com",
        "redtube.com", "www.redtube.com",
        "xnxx.com", "www.xnxx.com",
        "youporn.com", "www.youporn.com",
        "chaturbate.com", "www.chaturbate.com",
        "onlyfans.com", "www.onlyfans.com",
        "rule34.xxx", "www.rule34.xxx",
        "hentaihaven.red", "www.hentaihaven.red",
        "erome.com", "www.erome.com",
        "stripchat.com", "www.stripchat.com",
        "x.com", "www.x.com",
        "twitter.com", "www.twitter.com",
        "tiktok.com", "www.tiktok.com",
        "instagram.com", "www.instagram.com"
    };

    const std::vector<std::wstring> DEFAULT_TRIGGER_KEYWORDS = {
        L"porn", L"hentai", L"xvideos", L"pornhub", L"redtube", L"xnxx", 
        L"adulto", L"sexo", L"sexy", L"onlyfans", L"chaturbate", L"rule34",
        L"x.com", L"twitter", L"tiktok", L"instagram"
    };

    const std::vector<std::wstring> MONITORED_BROWSERS = {
        L"chrome.exe",
        L"firefox.exe",
        L"msedge.exe",
        L"brave.exe",
        L"opera.exe",
        L"vivaldi.exe",
        L"iexplore.exe",
        L"safari.exe"
    };

    const wchar_t* const CHROMIUM_POLICY_PATHS[] = {
        L"SOFTWARE\\Policies\\Google\\Chrome",
        L"SOFTWARE\\Policies\\Microsoft\\Edge",
        L"SOFTWARE\\Policies\\BraveSoftware\\Brave",
        L"SOFTWARE\\Policies\\Vivaldi",
        L"SOFTWARE\\Policies\\Opera Software\\Opera"
    };
    const int CHROMIUM_POLICY_COUNT = 5;

    const wchar_t FIREFOX_DOH_POLICY_PATH[] = L"SOFTWARE\\Policies\\Mozilla\\Firefox\\DNSOverHTTPS";

    const wchar_t WINDOWS_DNS_PARAMS_PATH[] = L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters";

    const std::string STREAK_DATA_DIR = "C:\\ProgramData\\Antx";
    const std::string STREAK_DATA_FILE = "C:\\ProgramData\\Antx\\streak.dat";
    const std::wstring STREAK_DATA_DIR_W = L"C:\\ProgramData\\Antx";
    const std::wstring DASHBOARD_HTML_FILE = L"C:\\ProgramData\\Antx\\dashboard.html";

    const wchar_t AUTOSTART_REG_PATH[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t AUTOSTART_REG_VALUE[] = L"AntxMonitor";

}

#endif
