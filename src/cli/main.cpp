#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <bcrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include "config.h"
#include <thread>
#include <chrono>

#pragma comment(lib, "bcrypt.lib")

std::vector<BYTE> GenerateRandomBytes(ULONG length) {
    std::vector<BYTE> buffer(length);
    BCRYPT_ALG_HANDLE hAlg = nullptr;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RNG_ALGORITHM, nullptr, 0) == 0) {
        BCryptGenRandom(hAlg, buffer.data(), length, 0);
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return buffer;
}

std::wstring BytesToHexString(const std::vector<BYTE>& bytes) {
    std::wstringstream wss;
    for (BYTE b : bytes) {
        wss << std::hex << std::setw(2) << std::setfill(L'0') << (int)b;
    }
    return wss.str();
}

std::vector<BYTE> HexStringToBytes(const std::wstring& hexStr) {
    std::vector<BYTE> bytes;
    for (size_t i = 0; i < hexStr.length(); i += 2) {
        std::wstring part = hexStr.substr(i, 2);
        BYTE b = (BYTE)wcstoul(part.c_str(), nullptr, 16);
        bytes.push_back(b);
    }
    return bytes;
}

std::wstring HashPassword(const std::wstring& password, const std::vector<BYTE>& salt) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<BYTE> hashResult;
    DWORD cbHashObject = 0, cbHash = 0, cbData = 0;

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, password.c_str(), (int)password.length(), nullptr, 0, nullptr, nullptr);
    std::vector<char> pwdBytes(sizeNeeded);
    WideCharToMultiByte(CP_UTF8, 0, password.c_str(), (int)password.length(), pwdBytes.data(), sizeNeeded, nullptr, nullptr);

    std::vector<BYTE> inputData;
    inputData.insert(inputData.end(), pwdBytes.begin(), pwdBytes.end());
    inputData.insert(inputData.end(), salt.begin(), salt.end());

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return L"";
    }

    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    std::vector<BYTE> pbHashObject(cbHashObject);

    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &cbData, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    hashResult.resize(cbHash);

    if (BCryptCreateHash(hAlg, &hHash, pbHashObject.data(), cbHashObject, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    if (BCryptHashData(hHash, inputData.data(), (ULONG)inputData.size(), 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    if (BCryptFinishHash(hHash, hashResult.data(), cbHash, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return BytesToHexString(hashResult);
}

bool SavePasswordToRegistry(const std::wstring& hash, const std::wstring& saltHex) {
    HKEY hKey;
    DWORD disposition;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, antx::REGISTRY_PATH, 0, nullptr, 
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disposition) == ERROR_SUCCESS) {

        RegSetValueExW(hKey, antx::REG_VAL_HASH, 0, REG_SZ, 
                       (const BYTE*)hash.c_str(), (DWORD)(hash.length() + 1) * sizeof(wchar_t));

        RegSetValueExW(hKey, antx::REG_VAL_SALT, 0, REG_SZ, 
                       (const BYTE*)saltHex.c_str(), (DWORD)(saltHex.length() + 1) * sizeof(wchar_t));

        DWORD authVal = 0;
        RegSetValueExW(hKey, antx::REG_VAL_UNINSTALL_AUTH, 0, REG_DWORD, 
                       (const BYTE*)&authVal, sizeof(DWORD));

        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool VerifyPassword(const std::wstring& inputPassword) {
    HKEY hKey;
    wchar_t savedHash[256] = {0};
    wchar_t savedSaltHex[256] = {0};
    DWORD sizeHash = sizeof(savedHash);
    DWORD sizeSalt = sizeof(savedSaltHex);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::REGISTRY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        LONG res1 = RegQueryValueExW(hKey, antx::REG_VAL_HASH, nullptr, nullptr, (LPBYTE)savedHash, &sizeHash);
        LONG res2 = RegQueryValueExW(hKey, antx::REG_VAL_SALT, nullptr, nullptr, (LPBYTE)savedSaltHex, &sizeSalt);
        RegCloseKey(hKey);

        if (res1 == ERROR_SUCCESS && res2 == ERROR_SUCCESS) {
            std::vector<BYTE> salt = HexStringToBytes(savedSaltHex);
            std::wstring computedHash = HashPassword(inputPassword, salt);
            return (computedHash == savedHash);
        }
    }
    return false;
}

bool SetUninstallAuthorized(bool authorized) {
    HKEY hKey;
    DWORD value = authorized ? 1 : 0;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::REGISTRY_PATH, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegSetValueExW(hKey, antx::REG_VAL_UNINSTALL_AUTH, 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void CleanHostsFile() {
    std::ifstream inFile(antx::HOSTS_FILE_PATH);
    std::vector<std::string> lines;
    std::string line;
    bool inBlock = false;

    if (inFile.is_open()) {
        while (std::getline(inFile, line)) {
            if (line.find(antx::BLOCK_MARKER_START) != std::string::npos) {
                inBlock = true;
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
        outFile.close();
    }
}

std::wstring GetCurrentExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"" : path.substr(0, pos);
}

bool InstallService() {
    SC_HANDLE schSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!schSCManager) {
        std::wcerr << L"[Erro] Nao foi possivel abrir o Service Control Manager. Execute como Administrador." << std::endl;
        return false;
    }

    std::wstring binPath = GetCurrentExecutableDirectory() + L"\\antx_service.exe";

    std::ifstream checkFile(binPath.c_str());
    if (!checkFile.good()) {
        std::wcerr << L"[Erro] Arquivo 'antx_service.exe' nao encontrado em: " << binPath << std::endl;
        std::wcerr << L"Certifique-se de que compilou ambos os executaveis e que estao na mesma pasta." << std::endl;
        CloseServiceHandle(schSCManager);
        return false;
    }
    checkFile.close();

    SC_HANDLE schService = CreateServiceW(
        schSCManager,
        antx::SERVICE_NAME,
        antx::SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    );

    if (!schService) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            std::wcout << L"[Aviso] Servico ja cadastrado." << std::endl;
            schService = OpenServiceW(schSCManager, antx::SERVICE_NAME, SERVICE_ALL_ACCESS);
        } else {
            std::wcerr << L"[Erro] CreateService falhou. Codigo: " << err << std::endl;
            CloseServiceHandle(schSCManager);
            return false;
        }
    }

    if (schService) {
        SERVICE_DESCRIPTIONW sd;
        sd.lpDescription = const_cast<LPWSTR>(antx::SERVICE_DESCRIPTION);
        ChangeServiceConfig2W(schService, SERVICE_CONFIG_DESCRIPTION, &sd);

        SERVICE_FAILURE_ACTIONSW sfa;
        SC_ACTION actions[3];

        actions[0].Type = SC_ACTION_RESTART;
        actions[0].Delay = 1000;
        actions[1].Type = SC_ACTION_RESTART;
        actions[1].Delay = 1000;
        actions[2].Type = SC_ACTION_RESTART;
        actions[2].Delay = 1000;

        sfa.dwResetPeriod = 86400;
        sfa.lpRebootMsg = nullptr;
        sfa.lpCommand = nullptr;
        sfa.cActions = 3;
        sfa.lpsaActions = actions;

        ChangeServiceConfig2W(schService, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

        if (StartService(schService, 0, nullptr)) {
            std::wcout << L"[Sucesso] Servico antx_service iniciado com sucesso!" << std::endl;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_ALREADY_RUNNING) {
                std::wcout << L"[Info] Servico ja estava rodando." << std::endl;
            } else {
                std::wcerr << L"[Aviso] Falha ao iniciar servico imediatamente. Codigo: " << err << std::endl;
            }
        }

        CloseServiceHandle(schService);
    }

    {
        CreateDirectoryW(antx::STREAK_DATA_DIR_W.c_str(), nullptr);
        system("icacls \"C:\\ProgramData\\Antx\" /grant *S-1-1-0:(OI)(CI)F /T /Q >nul 2>nul");

        std::wstring streakFile = antx::STREAK_DATA_DIR_W + L"\\streak.dat";
        std::ifstream checkStreak(streakFile.c_str());
        if (!checkStreak.good()) {
            struct StreakData {
                long long startTime;
                long long lastResetTime;
                int resetCount;
                bool rameleiFlag;
            } initialData;
            auto now = std::chrono::system_clock::now();
            initialData.startTime = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            initialData.lastResetTime = 0;
            initialData.resetCount = 0;
            initialData.rameleiFlag = false;

            std::ofstream outStreak(streakFile.c_str(), std::ios::binary | std::ios::trunc);
            if (outStreak.is_open()) {
                outStreak.write((const char*)&initialData, sizeof(initialData));
                outStreak.close();
            }
        }
        system("icacls \"C:\\ProgramData\\Antx\\streak.dat\" /grant *S-1-1-0:F /Q >nul 2>nul");
    }

    std::wstring monitorPath = GetCurrentExecutableDirectory() + L"\\antx_monitor.exe";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(monitorPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::wcout << L"[Sucesso] Monitor de vitoria antx_monitor iniciado com sucesso!" << std::endl;
    } else {
        std::wcerr << L"[Aviso] Nao foi possivel iniciar o monitor GUI automaticamente. Voce pode inicia-lo manualmente: " << monitorPath << std::endl;
    }

    CloseServiceHandle(schSCManager);
    return true;
}

bool UninstallService() {
    SC_HANDLE schSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        std::wcerr << L"[Erro] Nao foi possivel abrir o Service Control Manager. Execute como Administrador." << std::endl;
        return false;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, antx::SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!schService) {
        std::wcout << L"[Aviso] Servico nao encontrado." << std::endl;
        CloseServiceHandle(schSCManager);
        return true;
    }

    SERVICE_FAILURE_ACTIONSW sfa;
    sfa.dwResetPeriod = 0;
    sfa.lpRebootMsg = nullptr;
    sfa.lpCommand = nullptr;
    sfa.cActions = 0;
    sfa.lpsaActions = nullptr;
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

    SERVICE_STATUS_PROCESS ssp;
    if (ControlService(schService, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp)) {
        std::wcout << L"[Info] Parando o servico..." << std::endl;

        DWORD cbBytesNeeded = 0;
        for (int i = 0; i < 20; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &cbBytesNeeded)) {
                if (ssp.dwCurrentState == SERVICE_STOPPED) {
                    break;
                }
            }
        }
    }

    if (DeleteService(schService)) {
        std::wcout << L"[Sucesso] Servico antx removido com sucesso!" << std::endl;
    } else {
        std::wcerr << L"[Erro] Falha ao excluir o servico do registro. Codigo: " << GetLastError() << std::endl;
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);

    _wsystem(L"taskkill /f /im antx_monitor.exe >nul 2>&1");
    std::wcout << L"[Sucesso] Monitor GUI finalizado!" << std::endl;

    {
        HKEY hKeyAutostart;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, antx::AUTOSTART_REG_PATH, 0, KEY_WRITE, &hKeyAutostart) == ERROR_SUCCESS) {
            RegDeleteValueW(hKeyAutostart, antx::AUTOSTART_REG_VALUE);
            RegCloseKey(hKeyAutostart);
            std::wcout << L"[Sucesso] Registro de inicializacao automatica removido!" << std::endl;
        }
    }

    {
        std::wstring streakFile = antx::STREAK_DATA_DIR_W + L"\\streak.dat";
        DeleteFileW(streakFile.c_str());
        RemoveDirectoryW(antx::STREAK_DATA_DIR_W.c_str());
        std::wcout << L"[Sucesso] Historico e dados de streak removidos!" << std::endl;
    }

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, antx::REGISTRY_PATH);

    for (int i = 0; i < antx::CHROMIUM_POLICY_COUNT; i++) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::CHROMIUM_POLICY_PATHS[i], 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"DnsOverHttpsMode");
            RegCloseKey(hKey);
        }
    }
    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::FIREFOX_DOH_POLICY_PATH, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"Enabled");
            RegDeleteValueW(hKey, L"Locked");
            RegCloseKey(hKey);
        }
    }
    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, antx::WINDOWS_DNS_PARAMS_PATH, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"EnableAutoDoh");
            RegCloseKey(hKey);
        }
    }
    std::wcout << L"[Sucesso] Politicas de DNS dos navegadores restauradas!" << std::endl;

    CleanHostsFile();
    std::wcout << L"[Sucesso] Arquivo hosts restaurado e limpo!" << std::endl;

    return true;
}

void ShowUsage() {
    std::wcout << L"================================================" << std::endl;
    std::wcout << L"    antx CLI - Painel de Controle e Seguranca    " << std::endl;
    std::wcout << L"================================================" << std::endl;
    std::wcout << L"Uso:" << std::endl;
    std::wcout << L"  antx install          - Registra o servico e inicia os bloqueios nativos." << std::endl;
    std::wcout << L"  antx set-password     - Configura uma senha para desinstalacao futura." << std::endl;
    std::wcout << L"  antx generate-lock    - Cria uma senha complexa de auto-controle instantaneo." << std::endl;
    std::wcout << L"  antx uninstall        - Solicita senha cadastrada e desinstala o antx." << std::endl;
    std::wcout << L"================================================" << std::endl;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        ShowUsage();
        return 1;
    }

    std::wstring command = argv[1];

    if (command == L"install") {
        std::wcout << L"Iniciando instalacao do antx..." << std::endl;
        if (InstallService()) {
            std::wcout << L"\n[Sucesso] antx instalado com sucesso!" << std::endl;
            std::wcout << L"Recomendacao: Execute agora 'antx set-password' para travar a desinstalacao." << std::endl;
        } else {
            std::wcerr << L"\n[Falha] Falha ao instalar. Verifique se rodou como Administrador." << std::endl;
        }
    } 
    else if (command == L"set-password") {
        std::wcout << L"Digite a senha de bloqueio desejada: ";
        std::wstring pwd1;
        std::getline(std::wcin, pwd1);

        if (pwd1.empty()) {
            std::wcerr << L"Erro: A senha nao pode ser vazia." << std::endl;
            return 1;
        }

        std::wcout << L"Confirme a senha: ";
        std::wstring pwd2;
        std::getline(std::wcin, pwd2);

        if (pwd1 != pwd2) {
            std::wcerr << L"Erro: As senhas digitadas nao conferem!" << std::endl;
            return 1;
        }

        std::vector<BYTE> salt = GenerateRandomBytes(16);
        std::wstring saltHex = BytesToHexString(salt);
        std::wstring hash = HashPassword(pwd1, salt);

        if (SavePasswordToRegistry(hash, saltHex)) {
            std::wcout << L"[Sucesso] Senha configurada e gravada com sucesso!" << std::endl;
        } else {
            std::wcerr << L"[Erro] Falha ao salvar a senha no Registro. Rode como Administrador." << std::endl;
        }
    } 
    else if (command == L"generate-lock") {
        std::wcout << L"=== GERAÇÃO DE SENHA DE AUTO-CONTROLE AGRESSIVO ===" << std::endl;
        std::wcout << L"Este modo gerara uma senha aleatoria ultra-complexa." << std::endl;
        std::wcout << L"Envie-a para um amigo de confianca ou anote e descarte." << std::endl;
        std::wcout << L"Deseja continuar? (S/N): ";

        wchar_t choice;
        std::wcin >> choice;
        if (towupper(choice) != L'S') {
            std::wcout << L"Cancelado." << std::endl;
            return 0;
        }

        std::vector<BYTE> randBytes = GenerateRandomBytes(16);
        std::wstring generatedPwd = L"";
        for (BYTE b : randBytes) {
            generatedPwd += (wchar_t)(33 + (b % 93));
        }

        std::vector<BYTE> salt = GenerateRandomBytes(16);
        std::wstring saltHex = BytesToHexString(salt);
        std::wstring hash = HashPassword(generatedPwd, salt);

        if (SavePasswordToRegistry(hash, saltHex)) {
            std::wcout << L"\n==============================================" << std::endl;
            std::wcout << L"SUA SENHA GERADA (COPIE E DESCARTE DEPOIS):" << std::endl;
            std::wcout << L"  " << generatedPwd << std::endl;
            std::wcout << L"==============================================" << std::endl;
            std::wcout << L"A protecao foi ativada com sucesso!" << std::endl;
        } else {
            std::wcerr << L"[Erro] Falha ao salvar no Registro. Execute como Administrador." << std::endl;
        }
    } 
    else if (command == L"uninstall") {
        std::wcout << L"Digite a senha cadastrada para autorizar a desinstalacao: ";
        std::wstring inputPwd;
        std::getline(std::wcin, inputPwd);

        if (VerifyPassword(inputPwd)) {
            std::wcout << L"[Aprovado] Senha validada. Autorizando remocao..." << std::endl;

            SetUninstallAuthorized(true);

            if (UninstallService()) {
                std::wcout << L"[Sucesso] antx desinstalado com sucesso do sistema." << std::endl;
            } else {
                std::wcerr << L"[Erro] Houve um problema na remocao do servico." << std::endl;
            }
        } else {
            std::wcerr << L"\n[ACESSO NEGADO] Senha incorreta! O antx continua ativo e protegendo seu sistema." << std::endl;
        }
    } 
    else {
        ShowUsage();
    }

    return 0;
}
