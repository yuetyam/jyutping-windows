#ifndef AppVersion
#define AppVersion "0.2.0"
#endif

#ifndef OutputDir
#define OutputDir "output"
#endif

#ifndef VersionInfoVersion
#define VersionInfoVersion "0.2.0.0"
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
VersionInfoDescription=Jyutping Installer
VersionInfoProductName=Jyutping
VersionInfoProductVersion={#AppVersion}
VersionInfoVersion={#VersionInfoVersion}

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

[Code]
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
