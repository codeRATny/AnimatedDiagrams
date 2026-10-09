# Windows installer check (CI): an older setup is updated in place by the new one, a downgrade is
# refused, the uninstaller removes everything. Run elevated (setups install for all users).
#   ci/test-windows-installer.ps1 -Old dist-old/animated-diagrams-0.0.1-win64.exe -New dist/animated-diagrams-X.Y.Z-win64.exe
param(
    [Parameter(Mandatory)][string]$Old,
    [Parameter(Mandatory)][string]$New
)
$ErrorActionPreference = 'Stop'

$Key = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\AnimatedDiagrams_is1'
$Log = Join-Path ([IO.Path]::GetTempPath()) 'ad-setup.log'

function Invoke-Setup([string]$Exe, [string[]]$Extra = @()) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$Log`"") + $Extra
    $p = Start-Process -FilePath $Exe -ArgumentList $arguments -Wait -PassThru
    Write-Host "$(Split-Path $Exe -Leaf) $($Extra -join ' ') -> exit code $($p.ExitCode)"
    return $p.ExitCode
}

function Get-Installed {
    $k = Get-ItemProperty -Path $Key -ErrorAction SilentlyContinue
    if ($k) { return [pscustomobject]@{ Version = $k.DisplayVersion; Folder = $k.InstallLocation.TrimEnd('\') } }
    return $null
}

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        Write-Host "::error::$Message"
        if (Test-Path $Log) { Get-Content $Log -Tail 40 }
        exit 1
    }
}

$newVersion = [regex]::Match((Split-Path $New -Leaf), '\d+\.\d+\.\d+').Value
Assert ($newVersion -ne '') "no version in $New"
Assert ($null -eq (Get-Installed)) 'Animated Diagrams is already installed on the runner'

# 1. the old version
Assert ((Invoke-Setup $Old) -eq 0) 'installing the old version failed'
$installed = Get-Installed
Assert ($installed.Version -eq '0.0.1') "old version not registered: $($installed.Version)"
$folder = $installed.Folder
$exe = Join-Path $folder 'bin\animated-diagrams.exe'
Assert (Test-Path $exe) "$exe is missing"
$stale = Join-Path $folder 'bin\stale-from-old-version.dll'
Set-Content -Path $stale -Value 'left by an old version'

# 2. update while the application is running: it is closed, files are replaced, the folder is kept
$app = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 5
Assert ((Invoke-Setup $New) -eq 0) 'updating failed'
$installed = Get-Installed
Assert ($installed.Version -eq $newVersion) "version after the update: $($installed.Version), expected $newVersion"
Assert ($installed.Folder -eq $folder) "the update moved the installation: $($installed.Folder)"
Assert (Test-Path $exe) 'the application is missing after the update'
Assert (-not (Test-Path $stale)) 'a file of the old version was left behind'
Assert ($app.HasExited) 'the running application was not closed by the update'
$entries = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' |
             Where-Object { (Get-ItemProperty $_.PSPath).DisplayName -like 'Animated Diagrams*' })
Assert ($entries.Count -eq 1) "$($entries.Count) entries in Apps & features"

# 3. the old setup does not replace the newer version (silent: refused)
Assert ((Invoke-Setup $Old) -ne 0) 'a silent downgrade was not refused'
Assert ((Get-Installed).Version -eq $newVersion) 'the newer version was replaced'

# 4. reinstalling the same version
Assert ((Invoke-Setup $New) -eq 0) 'reinstalling failed'

# 5. uninstall
$uninstaller = Join-Path $folder 'unins000.exe'
$p = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
Assert ($p.ExitCode -eq 0) "uninstall exit code $($p.ExitCode)"
Start-Sleep -Seconds 3 # the uninstaller removes itself from a temporary copy
Assert ($null -eq (Get-Installed)) 'uninstall left the registry entry'
Assert (-not (Test-Path (Join-Path $folder 'bin'))) "uninstall left $folder\bin"

Write-Host "Installer OK: 0.0.1 -> $newVersion in $folder, downgrade refused, uninstalled"
