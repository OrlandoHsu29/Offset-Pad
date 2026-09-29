#ifndef MyAppVersion
  #define MyAppVersion "0.2.6"
#endif

#define MyAppName "Offset Pad"
#define MyAppExeName "Offset Pad.exe"

[Setup]
AppId={{BB2B99D0-A524-4447-B674-739A8D74AA6F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
DefaultDirName={localappdata}\Programs\Offset Pad
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputDir=..\dist
OutputBaseFilename=Offset-Pad-v{#MyAppVersion}-windows-x64-setup
SetupIconFile=..\media\OffsetPad-light.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Installer

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
english.AutoStartTask=Start automatically when Windows starts
english.LaunchProgram=Launch Offset Pad
chinesesimp.AutoStartTask=开机时启动
chinesesimp.LaunchProgram=运行 Offset Pad

[Tasks]
Name: "autostart"; Description: "{cm:AutoStartTask}"; Flags: checkedonce

[Files]
Source: "..\build\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Offset Pad"; ValueData: """{app}\{#MyAppExeName}"" --background"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent

[Code]
const
  AppWindowClass = 'OffsetPadMessageWindow';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'Offset Pad';
  PreferencesKey = 'Software\Offset Pad';
  WM_CLOSE = $0010;

procedure StopRunningApp;
var
  AppWindow: HWND;
  Attempt: Integer;
begin
  AppWindow := FindWindowByClassName(AppWindowClass);
  if AppWindow = 0 then
    Exit;

  PostMessage(AppWindow, WM_CLOSE, 0, 0);
  for Attempt := 1 to 40 do
  begin
    Sleep(50);
    if FindWindowByClassName(AppWindowClass) = 0 then
      Exit;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunningApp;
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (not WizardIsTaskSelected('autostart')) then
    RegDeleteValue(HKCU, RunKey, RunValue);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopRunningApp;
    RegDeleteValue(HKCU, RunKey, RunValue);
    RegDeleteKeyIncludingSubkeys(HKCU, PreferencesKey);
  end;
end;
