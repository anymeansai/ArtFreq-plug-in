; The ArtFreq installer for Windows (by AnyMeans), for Inno Setup 6.3 or later.
;
;   ISCC /DAppVersion=<version> [/DBuildDir=<build-dir>] [/O<output-dir>] installer\windows\ArtFreq.iss
;
; Produces ArtFreq-<version>-Windows.exe (in out\ unless /O says otherwise): a standard Windows
; installer with a welcome page, the licence, a choice of the two plug-ins (both on by default)
; and a last page with the first steps. It installs ArtFreq.vst3 (the instrument) and
; "ArtFreq FX.vst3" (the effect) into C:\Program Files\Common Files\VST3, the folder the VST3
; standard fixes for 64-bit plug-ins (so there is no folder to choose), and registers an
; uninstaller under Settings > Apps (Installed apps). The uninstaller and its log live in
; C:\Program Files\AnyMeans\ArtFreq, never in the VST3 folder.
;
; <build-dir> is a CMake build folder with a Release build of the ArtFreq and ArtFreqFX targets
; (default: build\ at the top of the repository). CI passes the version from CMakeLists.txt
; (project(ArtFreq VERSION x.y.z)). The installer is not code-signed, so Windows SmartScreen
; warns before running it ("Windows protected your PC": More info, then Run anyway).
;
; Silent, as CI runs it: ArtFreq-<version>-Windows.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
; /LOG="install.log"; the uninstaller (unins000.exe) takes the same switches.

#if Ver < EncodeVer(6, 3, 0, 0)
  #error Inno Setup 6.3 or later is needed (for x64compatible).
#endif

#ifndef AppVersion
  #error Pass the version: ISCC /DAppVersion=x.y.z ArtFreq.iss (CI takes it from CMakeLists.txt).
#endif

#ifndef BuildDir
  #define BuildDir AddBackslash(SourcePath) + "..\..\build"
#endif

; The two bundles as CMake builds them (the effect's name has a space).
#define ArtFreqBundle BuildDir + "\ArtFreq_artefacts\Release\VST3\ArtFreq.vst3"
#define ArtFreqFXBundle BuildDir + "\ArtFreqFX_artefacts\Release\VST3\ArtFreq FX.vst3"

#if !FileExists(ArtFreqBundle + "\Contents\x86_64-win\ArtFreq.vst3")
  #error ArtFreq.vst3 is not built: build the ArtFreq target in Release first (or pass /DBuildDir=...).
#endif
#if !FileExists(ArtFreqFXBundle + "\Contents\x86_64-win\ArtFreq FX.vst3")
  #error ArtFreq FX.vst3 is not built: build the ArtFreqFX target in Release first (or pass /DBuildDir=...).
#endif

[Setup]
; The installer's identity: Windows finds an earlier ArtFreq by it (to upgrade it in place, with
; one entry under Installed apps and one uninstaller). It must never change. (The first brace is
; doubled: a single one would start an Inno Setup constant.)
AppId={{8D861029-3188-4297-B978-7DA3D9DD561A}
AppName=ArtFreq
AppVersion={#AppVersion}
AppVerName=ArtFreq {#AppVersion}
AppPublisher=AnyMeans
AppPublisherURL=https://anymeansaudio.com
AppSupportURL=mailto:support@anymeansaudio.com
AppContact=support@anymeansaudio.com
AppCopyright=Copyright (c) 2026 AnyMeans
VersionInfoVersion={#AppVersion}
VersionInfoCompany=AnyMeans
VersionInfoDescription=ArtFreq installer
VersionInfoProductName=ArtFreq

; 64-bit plug-ins only: x64 Windows, or Windows 11 on Arm (which runs x64 hosts and plug-ins).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin

; The uninstaller's folder. The plug-ins go to the VST3 folder ([Files]), so there is nothing to
; choose: no folder page, no Start menu page.
DefaultDirName={commonpf64}\AnyMeans\ArtFreq
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
UninstallDisplayName=ArtFreq
UninstallDisplayIcon={uninstallexe}

; A DAW with a plug-in loaded holds its DLL (named .vst3, not .dll): find such a DAW and offer to
; close it, but do not start it again by itself afterwards (the last page says to rescan).
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.chm,*.vst3
RestartApplications=no

LicenseFile=..\..\LICENSE
WizardStyle=modern
; Several sizes of each picture (made by make-images.py): Setup picks the one nearest the size
; its window needs on this screen.
WizardImageFile=images\wizard-164x314.bmp,images\wizard-202x386.bmp,images\wizard-240x459.bmp,images\wizard-315x604.bmp,images\wizard-416x797.bmp,images\wizard-534x1022.bmp
WizardSmallImageFile=images\small-58.bmp,images\small-71.bmp,images\small-85.bmp,images\small-97.bmp,images\small-112.bmp,images\small-124.bmp,images\small-147.bmp,images\small-159.bmp
SetupIconFile=images\ArtFreq.ico
; Always write a log to %TEMP% (Setup Log <date>.txt), for support.
SetupLogging=yes

OutputDir=..\..\out
OutputBaseFilename=ArtFreq-{#AppVersion}-Windows
Compression=lzma2/max
SolidCompression=yes

[Types]
Name: "full"; Description: "Both plug-ins"
Name: "custom"; Description: "Only the plug-ins chosen below"; Flags: iscustom

[Components]
Name: "artfreq"; Description: "ArtFreq (instrument), for MIDI and instrument tracks"; Types: full
Name: "artfreqfx"; Description: "ArtFreq FX (audio effect), for audio tracks, buses and the master"; Types: full

[InstallDelete]
; First, before anything is copied: each plug-in's earlier copy goes whole, so no file of an
; older version stays inside the new bundle, and so do the bundles from before 0.4, when the
; plug-ins were called FreqArt and FreqArt FX. Those have the same plug-in IDs (old projects open
; with ArtFreq), so a DAW would list each plug-in twice. A plug-in left out keeps its copy.
Type: filesandordirs; Name: "{commoncf64}\VST3\ArtFreq.vst3"; Components: artfreq
Type: filesandordirs; Name: "{commoncf64}\VST3\FreqArt.vst3"; Components: artfreq
Type: filesandordirs; Name: "{commoncf64}\VST3\ArtFreq FX.vst3"; Components: artfreqfx
Type: filesandordirs; Name: "{commoncf64}\VST3\FreqArt FX.vst3"; Components: artfreqfx

[Files]
; Each bundle is a folder: <name>.vst3\Contents\x86_64-win\<name>.vst3 (the 64-bit DLL) and
; Contents\Resources\moduleinfo.json. Copied whole, always (no version comparison).
Source: "{#ArtFreqBundle}\*"; DestDir: "{commoncf64}\VST3\ArtFreq.vst3"; Components: artfreq; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArtFreqFXBundle}\*"; DestDir: "{commoncf64}\VST3\ArtFreq FX.vst3"; Components: artfreqfx; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; The uninstaller removes what it installed; these make sure nothing of the two bundles stays in
; the VST3 folder even if something was added inside them. The activation, in
; C:\ProgramData\AnyMeans\Licenses.json, is not ArtFreq's to delete: it is shared by every
; AnyMeans plug-in, and keeping it means a reinstall is still activated.
Type: filesandordirs; Name: "{commoncf64}\VST3\ArtFreq.vst3"; Components: artfreq
Type: filesandordirs; Name: "{commoncf64}\VST3\ArtFreq FX.vst3"; Components: artfreqfx

[Messages]
WelcomeLabel1=Welcome to ArtFreq
WelcomeLabel2=ArtFreq by AnyMeans turns words, pictures and brush strokes into sound. Look at that sound in a spectrogram (an analyser such as MiniMeters) and you see your drawing.%n%nThis installs two plug-ins from the same code, for 64-bit VST3 hosts such as Ableton Live, FL Studio, Reaper, Cubase, Studio One and Bitwig:%n%n- ArtFreq, an instrument for MIDI tracks: it plays the drawing.%n- ArtFreq FX, an audio effect for audio tracks, buses and the master: it carves the drawing into your song.%n%nClose your DAW before you continue.
SelectComponentsDesc=Which plug-ins should be installed?
SelectComponentsLabel2=Both are installed by default; clear one to leave it out. They go into C:\Program Files\Common Files\VST3, where VST3 hosts look for plug-ins.
FinishedHeadingLabel=ArtFreq is installed
FinishedLabelNoIcons=Open your DAW and rescan plug-ins. ArtFreq is under instruments, ArtFreq FX under audio effects.%n%nAbleton Live: Preferences (Settings in Live 12) > Plug-ins > Use VST3 Plug-In System Folders: On, then Rescan.%n%nThen open the plug-in's window and activate it with the key from your purchase email (once per computer; an ArtFreq Pro key activates both plug-ins).

[Code]
{ LICENSE is wrapped at 100 columns, wider than the licence page, which would break every line
  in two. Join each paragraph's lines so the page wraps them to its own width. }
procedure ReflowLicence;
var
  S: String;
begin
  S := WizardForm.LicenseMemo.Text;
  StringChangeEx(S, #13#10, #10, True);
  StringChangeEx(S, #13, #10, True);
  StringChangeEx(S, #10#10, #1, True);
  StringChangeEx(S, #10, ' ', True);
  StringChangeEx(S, #1, #13#10#13#10, True);
  WizardForm.LicenseMemo.Text := Trim(S);
end;

procedure InitializeWizard;
begin
  ReflowLicence;
end;

{ Installing nothing is not a choice. }
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and (WizardSelectedComponents(False) = '') then begin
    SuppressibleMsgBox('Choose at least one plug-in: ArtFreq, ArtFreq FX or both.', mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

{ The Ready page says where the plug-ins go (the folder Setup would show is only the
  uninstaller's). }
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := 'Plug-ins, into ' + ExpandConstant('{commoncf64}\VST3') + ':' + NewLine;
  if WizardIsComponentSelected('artfreq') then
    Result := Result + Space + 'ArtFreq.vst3 (the instrument)' + NewLine;
  if WizardIsComponentSelected('artfreqfx') then
    Result := Result + Space + 'ArtFreq FX.vst3 (the audio effect)' + NewLine;
  Result := Result + Space + 'An earlier ArtFreq (or FreqArt) there is replaced.' + NewLine +
    NewLine + 'Uninstaller:' + NewLine +
    Space + ExpandConstant('{app}') + NewLine +
    Space + 'To remove ArtFreq later: Settings > Apps.';
end;
