// Animated Diagrams setup: included by the CPack Inno Setup generator at the end of the [Code]
// section (CPACK_INNOSETUP_CODE_FILES, cmake/Packaging.cmake).
//
// One installation per machine, identified by AppId "AnimatedDiagrams": a newer setup updates it
// (the folder, shortcuts and tasks of the previous installation are reused, the license, folder and
// tasks pages are skipped); an older setup asks before replacing a newer version (silent setups
// refuse unless /ALLOWDOWNGRADE is given).

const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\AnimatedDiagrams_is1';

var
  InstalledVersion: String; // empty -- not installed
  InstalledFolder: String;

// Next dot separated number of S (removed from S).
function NextVersionNumber(var S: String): Integer;
var
  P: Integer;
begin
  P := Pos('.', S);
  if P = 0 then
  begin
    Result := StrToIntDef(Trim(S), 0);
    S := '';
  end
  else
  begin
    Result := StrToIntDef(Trim(Copy(S, 1, P - 1)), 0);
    Delete(S, 1, P);
  end;
end;

// -1 / 0 / 1 for A < B, A = B, A > B ("2.1.0" < "2.10.0").
function CompareVersions(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  for I := 1 to 4 do
  begin
    X := NextVersionNumber(A);
    Y := NextVersionNumber(B);
    if X < Y then
    begin
      Result := -1;
      Exit;
    end;
    if X > Y then
    begin
      Result := 1;
      Exit;
    end;
  end;
end;

function SetupVersion(): String;
begin
  Result := '{#SetupSetting("AppVersion")}';
end;

function HasParam(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
      Result := True;
end;

function IsUpdate(): Boolean;
begin
  Result := InstalledVersion <> '';
end;

// [InstallDelete] check: the folder being installed to is the one of the previous installation.
function UpdatingThisFolder(): Boolean;
begin
  Result := IsUpdate() and (InstalledFolder <> '') and
            (CompareText(RemoveBackslashUnlessRoot(InstalledFolder), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not RegQueryStringValue(HKA, UninstallKey, 'DisplayVersion', InstalledVersion) then
    InstalledVersion := ''
  else if not RegQueryStringValue(HKA, UninstallKey, 'InstallLocation', InstalledFolder) then
    InstalledFolder := '';
  if IsUpdate() then
    Log(Format('Installed version %s in "%s", this setup: %s', [InstalledVersion, InstalledFolder, SetupVersion()]));
  if IsUpdate() and (CompareVersions(InstalledVersion, SetupVersion()) > 0) and not HasParam('/ALLOWDOWNGRADE') then
  begin
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('DowngradeQuestion'), [InstalledVersion, SetupVersion()]),
                                 mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
    if not Result then
      Log('Not replacing a newer version');
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // an update keeps the accepted license, the folder and the chosen tasks (desktop icon)
  Result := IsUpdate() and ((PageID = wpLicense) or (PageID = wpSelectDir) or (PageID = wpSelectProgramGroup) or (PageID = wpSelectTasks));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpReady) and IsUpdate() then
  begin
    if CompareVersions(InstalledVersion, SetupVersion()) = 0 then
      WizardForm.ReadyLabel.Caption := FmtMessage(CustomMessage('ReinstallNote'), [InstalledVersion])
    else
      WizardForm.ReadyLabel.Caption := FmtMessage(CustomMessage('UpdateNote'), [InstalledVersion, SetupVersion()]);
  end;
end;
