[Setup]
AppName=Antx Blocker
AppVersion={#MyAppVersion}
AppPublisher=Antigravity Engine
DefaultDirName={autopf}\Antx
DefaultGroupName=Antx Blocker
OutputBaseFilename=Antx_Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
SetupIconFile=compiler:SetupClassicIcon.ico

[Files]
Source: "build\bin\antx.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\bin\antx_service.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\bin\antx_monitor.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Antx CLI"; Filename: "{app}\antx.exe"
Name: "{group}\Antx Dashboard"; Filename: "{app}\antx_monitor.exe"
Name: "{group}\Desinstalar Antx"; Filename: "{uninstallexe}"

[Run]
; Roda a instalação do serviço e inicialização após a cópia dos arquivos
Filename: "{app}\antx.exe"; Parameters: "install"; Flags: runhidden runascurrentuser

[UninstallRun]
; Desinstala o serviço e para tudo antes de deletar
Filename: "{app}\antx.exe"; Parameters: "uninstall"; Flags: runhidden runascurrentuser

[Code]
// Função chamada pelo Inno Setup antes da instalação
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  // Para o serviço se já estiver instalado, para liberar os arquivos para sobrescrita
  Exec('cmd.exe', '/c net stop antx', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM antx_monitor.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := True;
end;

// Função chamada antes da desinstalação
function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('cmd.exe', '/c net stop antx', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM antx_monitor.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := True;
end;
