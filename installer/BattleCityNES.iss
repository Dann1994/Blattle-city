; Instalador de Battle City NES (Inno Setup 6). Compilar con installer/build_installer.sh (le pasa la version: /DAppVersion=1.0.0).
; Instala solo para el usuario actual (no pide permisos de administrador) en %LOCALAPPDATA%\Programs\BattleCityNES, crea los accesos
; del menu Inicio (y opcionalmente del escritorio) y registra el desinstalador en "Aplicaciones instaladas".
; Las actualizaciones automaticas del juego descargan este mismo instalador y lo ejecutan en silencio (/SILENT): reemplaza los archivos y
; vuelve a abrir el juego. Los niveles propios y los ajustes (carpeta niveles\ y battlecitynes_settings.txt) no se tocan al actualizar.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppName "Battle City NES"
#define AppExe "battlecitynes.exe"

[Setup]
AppId={{B7A2D8C4-5E0F-4C61-9A3B-7D1E5F20C9A4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Dann1994
AppPublisherURL=https://github.com/Dann1994/Blattle-city
AppSupportURL=https://github.com/Dann1994/Blattle-city/issues
AppUpdatesURL=https://github.com/Dann1994/Blattle-city/releases
DefaultDirName={autopf}\BattleCityNES
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\assets\icon.ico
OutputDir=..\dist
OutputBaseFilename=BattleCityNES-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
; el acceso directo del escritorio viene tildado (se puede destildar en el asistente)
Name: "desktopicon"; Description: "Crear un acceso directo en el &escritorio"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\dist\BattleCityNES\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\BattleCityNES\level_editor.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\BattleCityNES\LEEME.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\BattleCityNES\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; todos los accesos llevan el icono del juego (el tanque amarillo, assets\icon.ico)
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\assets\icon.ico"; Tasks: desktopicon
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\assets\icon.ico"
Name: "{autoprograms}\{#AppName} - Editor de niveles"; Filename: "{app}\level_editor.exe"; IconFilename: "{app}\assets\icon.ico"
Name: "{autoprograms}\Desinstalar {#AppName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\assets\icon.ico"

[Run]
; en la instalacion normal es una casilla al final; en la actualizacion silenciosa (/SILENT) el juego se vuelve a abrir solo
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall

[Code]
// Al desinstalar: se pregunta si tambien se borran los niveles propios y los ajustes (por defecto se conservan).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if DirExists(ExpandConstant('{app}\niveles')) or FileExists(ExpandConstant('{app}\battlecitynes_settings.txt')) then
    begin
      if MsgBox('¿Querés borrar también tus niveles propios y tus ajustes del juego?' + #13#10 + 'Si elegís No, quedan guardados por si lo volvés a instalar.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(ExpandConstant('{app}\niveles'), True, True, True);
        DeleteFile(ExpandConstant('{app}\battlecitynes_settings.txt'));
        RemoveDir(ExpandConstant('{app}'));
      end;
    end;
  end;
end;
