#ifndef AppVersion
#error Build installers with packaging\Build-Installers.ps1 so versions match the compiled payloads.
#endif

#ifndef OutputDir
#define OutputDir "output"
#endif

#ifndef VersionInfoVersion
#error VersionInfoVersion must be supplied by packaging\Build-Installers.ps1.
#endif

#define AppId "{{B120D0CC-C4A1-4F0A-BA46-B3F1376BDE4F}"

[Setup]
AppId={#AppId}
AppName=Jyutping
AppVersion={#AppVersion}
AppPublisher=Jyutping.org
DefaultDirName={autopf64}\Jyutping
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputDir={#OutputDir}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
UninstallDisplayName=Jyutping
UninstallDisplayIcon={app}\Jyutping.dll
UninstallFilesDir={app}
VersionInfoCompany=Jyutping.org
VersionInfoCopyright=CC0 1.0 Public Domain
VersionInfoDescription=Jyutping Installer ({#PackageArchitecture})
VersionInfoProductName=Jyutping
VersionInfoProductVersion={#AppVersion}
VersionInfoVersion={#VersionInfoVersion}
#if PackageArchitecture == "arm64"
MinVersion=10.0.22000
#else
MinVersion=10.0
#endif
CloseApplications=no
RestartApplications=no
SetupMutex=Jyutping.Install

[Files]
Source: "{#StageRoot}\Updater\JyutpingUpdater.exe"; DestDir: "{app}\Updater"; Flags: ignoreversion restartreplace
Source: "{#StageRoot}\Updater\WinSparkle.dll"; DestDir: "{app}\Updater"; Flags: ignoreversion restartreplace
Source: "{#StageRoot}\Updater\COPYING"; DestDir: "{app}\Updater"; Flags: ignoreversion
Source: "{#StageRoot}\Updater\COPYING.expat"; DestDir: "{app}\Updater"; Flags: ignoreversion
Source: "{#StageRoot}\Updater\AUTHORS"; DestDir: "{app}\Updater"; Flags: ignoreversion

[Registry]
Root: HKLM64; Subkey: "Software\Jyutping"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinese_traditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "chinese_simplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
english.RegistrationError=Could not register Jyutping.
english.UnregistrationError=Could not unregister Jyutping.
chinese_traditional.RegistrationError=無法註冊粵拼輸入法。
chinese_traditional.UnregistrationError=無法取消註冊粵拼輸入法。
chinese_simplified.RegistrationError=无法注册粤拼输入法。
chinese_simplified.UnregistrationError=无法取消注册粤拼输入法。
english.FilesInUse=Jyutping is still in use. Close applications using this input method and try again. If needed, switch to another input method and sign out or restart Windows before running the installer again.
chinese_traditional.FilesInUse=粵拼輸入法仍在使用中。請關閉使用此輸入法的應用程式再試。如有需要，請切換至其他輸入法，登出或重新啓動 Windows，再執行安裝程式。
chinese_simplified.FilesInUse=粤拼输入法仍在使用中。请关闭使用此输入法的应用程序再试。如有需要，请切换至其他输入法，注销或重新启动 Windows，再运行安装程序。

[Code]
function CanReplaceRuntimeFile(FileName: String): Boolean;
var
    Stream: TFileStream;
begin
    Result := True;
    if not FileExists(FileName) then
        exit;
    try
        Stream := TFileStream.Create(FileName, fmOpenReadWrite or fmShareExclusive);
        Stream.Free;
    except
        Log('Jyutping: runtime file is in use: ' + FileName + ': ' + GetExceptionMessage);
        Result := False;
    end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
    { Check before unregistering or copying anything. Never force-close IME host applications. }
    Result := '';
    if not CanReplaceRuntimeFile(ExpandConstant('{app}\Jyutping.dll')) or
       not CanReplaceRuntimeFile(ExpandConstant('{app}\ime.sqlite3')) or
       not CanReplaceRuntimeFile(ExpandConstant('{app}\Updater\JyutpingUpdater.exe')) or
       not CanReplaceRuntimeFile(ExpandConstant('{app}\Updater\WinSparkle.dll')) then
        Result := CustomMessage('FilesInUse');
    #if PackageArchitecture == "x64"
    if not CanReplaceRuntimeFile(ExpandConstant('{autopf32}\Jyutping\Jyutping.dll')) or
       not CanReplaceRuntimeFile(ExpandConstant('{autopf32}\Jyutping\ime.sqlite3')) then
        Result := CustomMessage('FilesInUse');
    #endif
end;

function RegistrationActionName(RegisterDll: Boolean): string;
begin
    if RegisterDll then
        Result := 'register'
    else
        Result := 'unregister';
end;

function RunRegSvr32(RegSvr32Path: string; DllPath: string; RegisterDll: Boolean; FailOnError: Boolean): Boolean;
var
    Parameters: string;
    ResultCode: Integer;
    ActionName: string;
begin
    Result := True;
    ActionName := RegistrationActionName(RegisterDll);

    if not FileExists(DllPath) then begin
        Log('Jyutping: skipping ' + ActionName + '; DLL is absent: ' + DllPath);
        exit;
    end;

    Parameters := '/s ';
    if not RegisterDll then
        Parameters := Parameters + '/u ';
    Parameters := Parameters + '"' + DllPath + '"';

    Log('Jyutping: attempting to ' + ActionName + ' DLL: ' + DllPath);
    if not Exec(RegSvr32Path, Parameters, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
        Log('Jyutping: failed to launch regsvr32 for ' + ActionName + ': ' + RegSvr32Path);
        Result := not FailOnError;
        exit;
    end;

    if ResultCode <> 0 then begin
        Log('Jyutping: regsvr32 exit code ' + IntToStr(ResultCode) + ' while trying to ' + ActionName + ': ' + DllPath);
        Result := not FailOnError;
    end;
end;

procedure RequireRegSvr32(RegSvr32Path: string; DllPath: string; RegisterDll: Boolean);
begin
    if not RunRegSvr32(RegSvr32Path, DllPath, RegisterDll, True) then begin
        if RegisterDll then
            MsgBox(CustomMessage('RegistrationError') + #13#10 + DllPath, mbError, MB_OK)
        else
            MsgBox(CustomMessage('UnregistrationError') + #13#10 + DllPath, mbError, MB_OK);
        Abort;
    end;
end;
