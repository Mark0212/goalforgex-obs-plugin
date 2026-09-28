; GoalForgeX for OBS — Windows installer (Inno Setup 6)
;
; Built by .github/scripts/Package-Windows.ps1 after the plugin is compiled:
;   ISCC /DSourceDir=<release\RelWithDebInfo\goalforgex> /DAppVersion=1.0.0 goalforgex.iss
; SourceDir is the template's install layout: bin\64bit\goalforgex.dll + data\.
;
; Plugin location (https://obsproject.com/kb/legacy-plugin-locations):
;   OBS 33+      C:\ProgramData\obs-studio\plugins\goalforgex\goalforgex.dll
;   OBS 31.1–32  C:\ProgramData\obs-studio\plugins\goalforgex\bin\64bit\goalforgex.dll  (legacy;
;                no longer loaded from OBS 34 — re-run this installer after upgrading OBS)
; `data\` sits at plugins\goalforgex\data in both layouts. Only ONE DLL location is ever
; left on disk, so OBS can never find and load the plugin twice.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\release\RelWithDebInfo\goalforgex"
#endif

[Setup]
AppId={{AD18D82D-A09C-4CC0-BE53-DE84E62FADE0}
AppName=GoalForgeX for OBS
AppVersion={#AppVersion}
AppVerName=GoalForgeX for OBS {#AppVersion}
AppPublisher=GoalForgeX
AppPublisherURL=https://goalforgex.com
AppSupportURL=https://goalforgex.com/support
AppUpdatesURL=https://goalforgex.com/obs
DefaultDirName={commonappdata}\obs-studio\plugins\goalforgex
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=.
OutputBaseFilename=goalforgex-{#AppVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=GoalForgeX for OBS
CloseApplications=no

[Messages]
FinishedLabel=GoalForgeX for OBS is installed.%n%nOpen OBS Studio and choose Docks → GoalForgeX, then click Connect GoalForgeX.

[Files]
; OBS 33+ (new layout)
Source: "{#SourceDir}\bin\64bit\goalforgex.dll"; DestDir: "{app}"; Flags: ignoreversion; Check: UseNewLayout
; OBS 31.1–32.x (legacy layout)
Source: "{#SourceDir}\bin\64bit\goalforgex.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion; Check: not UseNewLayout
Source: "{#SourceDir}\bin\64bit\goalforgex.pdb"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion skipifsourcedoesntexist; Check: not UseNewLayout
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[InstallDelete]
; Remove the OTHER layout's DLL (e.g. left over from before an OBS upgrade).
Type: files; Name: "{app}\goalforgex.dll"; Check: not UseNewLayout
Type: filesandordirs; Name: "{app}\bin"; Check: UseNewLayout

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
var
  ObsFound: Boolean;
  ObsMajor, ObsMinor: Integer;
  ObsVersionText: String;

function ObsExePath(): String;
var
  InstallDir: String;
begin
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', InstallDir) and (InstallDir <> '') then
    Result := AddBackslash(InstallDir) + 'bin\64bit\obs64.exe'
  else
    Result := ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe');
end;

procedure DetectObs();
var
  Exe, V: String;
  P: Integer;
begin
  ObsFound := False;
  ObsMajor := 0;
  ObsMinor := 0;
  Exe := ObsExePath();
  if FileExists(Exe) and GetVersionNumbersString(Exe, V) then
  begin
    ObsFound := True;
    ObsVersionText := V;
    P := Pos('.', V);
    if P > 0 then
    begin
      ObsMajor := StrToIntDef(Copy(V, 1, P - 1), 0);
      V := Copy(V, P + 1, Length(V));
      P := Pos('.', V);
      if P > 0 then
        ObsMinor := StrToIntDef(Copy(V, 1, P - 1), 0)
      else
        ObsMinor := StrToIntDef(V, 0);
    end;
  end;
end;

function UseNewLayout(): Boolean;
begin
  // Unknown version → legacy layout, which OBS 31.1–33 all load.
  Result := ObsFound and (ObsMajor >= 33);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  DetectObs();
  if not ObsFound then
  begin
    Result := MsgBox('OBS Studio wasn''t found in its usual place on this PC.' + #13#10#13#10 +
      'GoalForgeX for OBS needs OBS Studio 31.1 or newer (it does not work with Streamlabs Desktop). ' +
      'Install it anyway?', mbConfirmation, MB_YESNO) = IDYES;
  end
  else if (ObsMajor < 31) or ((ObsMajor = 31) and (ObsMinor < 1)) then
  begin
    MsgBox('GoalForgeX for OBS needs OBS Studio 31.1 or newer — this PC has ' + ObsVersionText + '.' + #13#10#13#10 +
      'Update OBS from obsproject.com, then run this installer again.', mbError, MB_OK);
    Result := False;
  end;
end;

function IsObsRunning(): Boolean;
var
  RC: Integer;
begin
  // find.exe exits 0 when obs64.exe appears in the task list.
  Result := Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq obs64.exe" /NH | find /I "obs64.exe" >NUL',
    '', SW_HIDE, ewWaitUntilTerminated, RC) and (RC = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  while IsObsRunning() do
  begin
    if MsgBox('OBS Studio is open. Close OBS completely, then click Retry.', mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := 'OBS Studio must be closed to install GoalForgeX for OBS.';
      Exit;
    end;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
  begin
    if MsgBox('OBS Studio is open. Close OBS completely, then click Retry.', mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;
