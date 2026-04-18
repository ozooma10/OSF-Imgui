[CmdletBinding()]
param(
    [ValidateSet('debug', 'releasedbg')]
    [string]$Mode = 'debug',
    [string]$MO2Path = 'C:\Modding\Starfield\MO2',
    [string]$ModsPath = '',
    [string]$ExecutableName = 'Starfield (SFSE)',
    [string]$GameProcessName = 'Starfield',
    [int]$LaunchTimeoutSeconds = 120,
    [int]$StableProcessSeconds = 5,
    [switch]$EmitPidOnly,
    [switch]$SkipBuild,
    [switch]$SkipLaunch,
    [switch]$SkipAttach,
    [switch]$ForceRestart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Status {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $EmitPidOnly) {
        Write-Host $Message
    }
}

function Get-IniValue {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [AllowEmptyString()]
        [string[]]$Lines,
        [Parameter(Mandatory = $true)]
        [string]$Key
    )

    $prefix = '{0}=' -f $Key
    $line = $Lines | Where-Object { $_.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
    if (-not $line) {
        return $null
    }

    return $line.Substring($prefix.Length)
}

function Decode-MO2ByteArray {
    param(
        [string]$Value
    )

    if (-not $Value) {
        return $null
    }

    if ($Value -match '^@ByteArray\((.*)\)$') {
        return $Matches[1]
    }

    return $Value
}

function Stop-StarfieldProcesses {
    param(
        [string[]]$Names
    )

    foreach ($name in $Names) {
        $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        foreach ($proc in $procs) {
            Write-Status ("[starfield-debug] stopping {0} (pid {1})" -f $proc.ProcessName, $proc.Id)
            try {
                Stop-Process -Id $proc.Id -Force -ErrorAction Stop
            } catch {
                Write-Status ("[starfield-debug] could not stop pid {0}: {1}" -f $proc.Id, $_.Exception.Message)
            }
        }
    }
}

function Get-LatestNewProcess {
    param(
        [string]$Name,
        [int[]]$IgnorePids
    )

    return Get-Process -Name $Name -ErrorAction SilentlyContinue |
        Where-Object { $IgnorePids -notcontains $_.Id } |
        Sort-Object StartTime -Descending |
        Select-Object -First 1
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$mo2Exe = Join-Path $MO2Path 'ModOrganizer.exe'
$mo2Ini = Join-Path $MO2Path 'ModOrganizer.ini'
$vsJitDebugger = Join-Path $env:WINDIR 'System32\vsjitdebugger.exe'
$stateDir = Join-Path $repoRoot '.xmake'
$stateFile = Join-Path $stateDir 'starfield-debug-session.json'
$stateEnvFile = Join-Path $stateDir 'starfield-debug-session.env'
$pluginName = 'starfield-re-sandbox'

if (-not (Test-Path $mo2Exe)) {
    throw ("Mod Organizer executable not found: {0}" -f $mo2Exe)
}

if (-not (Test-Path $mo2Ini)) {
    throw ("Mod Organizer config not found: {0}" -f $mo2Ini)
}

$mo2IniLines = Get-Content $mo2Ini
$selectedProfileValue = Get-IniValue -Lines $mo2IniLines -Key 'selected_profile'
$gamePathValue = Get-IniValue -Lines $mo2IniLines -Key 'gamePath'
$selectedProfile = Decode-MO2ByteArray -Value $selectedProfileValue
$gamePath = Decode-MO2ByteArray -Value $gamePathValue
$modsRoot = $ModsPath
if ([string]::IsNullOrWhiteSpace($modsRoot)) {
    $modsRoot = $env:XSE_SF_MODS_PATH
}
if ([string]::IsNullOrWhiteSpace($modsRoot)) {
    $modsRoot = Join-Path $MO2Path 'mods'
}

$pluginModPath = Join-Path $modsRoot $pluginName
$profileModList = $null
if ($selectedProfile) {
    $candidate = Join-Path (Join-Path $MO2Path 'profiles') (Join-Path $selectedProfile 'modlist.txt')
    if (Test-Path $candidate) {
        $profileModList = $candidate
    }
}

Set-Location $repoRoot

New-Item -ItemType Directory -Path $pluginModPath -Force | Out-Null
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null

if (-not (Test-Path $stateEnvFile)) {
    @(
        'gamePid=0'
        'gameProcess='
        'mode='
        'launchedAt='
    ) | Set-Content $stateEnvFile
}

$env:XSE_SF_MODS_PATH = $modsRoot
if (-not [string]::IsNullOrWhiteSpace($gamePath)) {
    $env:XSE_SF_GAME_PATH = $gamePath
}

Write-Status ("[starfield-debug] repo root: {0}" -f $repoRoot)
Write-Status ("[starfield-debug] MO2 path:  {0}" -f $MO2Path)
Write-Status ("[starfield-debug] mods path: {0}" -f $modsRoot)
if ($selectedProfile) {
    Write-Status ("[starfield-debug] profile:   {0}" -f $selectedProfile)
}

if ($profileModList) {
    $isEnabled = Select-String -Path $profileModList -SimpleMatch ("+{0}" -f $pluginName) -Quiet
    if (-not $isEnabled -and -not $EmitPidOnly) {
        Write-Warning ("MO2 profile '{0}' does not currently enable '{1}'. Enable it once in MO2 so the copied plugin is visible in-game." -f $selectedProfile, $pluginName)
    }
}

if ($ForceRestart) {
    Stop-StarfieldProcesses -Names @($GameProcessName, 'sfse_loader')
}

if (-not $SkipBuild) {
    Write-Status ("[starfield-debug] configuring xmake ({0})..." -f $Mode)
    if ($EmitPidOnly) {
        & xmake f -P $repoRoot -m $Mode | Out-Null
    } else {
        & xmake f -P $repoRoot -m $Mode
    }
    if ($LASTEXITCODE -ne 0) {
        throw ("xmake configure failed with exit code {0}" -f $LASTEXITCODE)
    }

    Write-Status ("[starfield-debug] building with xmake ({0})..." -f $Mode)
    if ($EmitPidOnly) {
        & xmake build -P $repoRoot | Out-Null
    } else {
        & xmake build -P $repoRoot
    }
    if ($LASTEXITCODE -ne 0) {
        throw ("xmake build failed with exit code {0}" -f $LASTEXITCODE)
    }
}

if ($SkipLaunch) {
    return 
}

$existingPids = @(Get-Process -Name $GameProcessName -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
if ($existingPids.Count -gt 0 -and -not $ForceRestart) {
    throw ("{0}.exe is already running. Re-run with -ForceRestart or stop the game first." -f $GameProcessName)
}

$mo2Argument = 'moshortcut://:{0}' -f $ExecutableName
Write-Status ("[starfield-debug] launching through MO2: {0}" -f $mo2Argument)
Start-Process -FilePath $mo2Exe -ArgumentList "`"$mo2Argument`"" | Out-Null

$deadline = (Get-Date).AddSeconds($LaunchTimeoutSeconds)
$gameProcess = $null

while ((Get-Date) -lt $deadline) {
    $gameProcess = Get-LatestNewProcess -Name $GameProcessName -IgnorePids $existingPids

    if ($gameProcess) {
        break
    }

    Start-Sleep -Milliseconds 500
}

if (-not $gameProcess) {
    throw ("Timed out after {0}s waiting for {1}.exe to launch from MO2." -f $LaunchTimeoutSeconds, $GameProcessName)
}

Write-Status ("[starfield-debug] detected {0}.exe pid: {1}; waiting for a stable process..." -f $gameProcess.ProcessName, $gameProcess.Id)

$stableDeadline = (Get-Date).AddSeconds($LaunchTimeoutSeconds)
$stablePid = $gameProcess.Id
$stableSince = Get-Date

while ((Get-Date) -lt $stableDeadline) {
    Start-Sleep -Milliseconds 500

    $latestProcess = Get-LatestNewProcess -Name $GameProcessName -IgnorePids $existingPids
    if (-not $latestProcess) {
        $stablePid = 0
        $stableSince = Get-Date
        continue
    }

    if ($latestProcess.Id -ne $stablePid) {
        Write-Status ("[starfield-debug] startup pid changed from {0} to {1}; waiting for the final game process..." -f $stablePid, $latestProcess.Id)
        $stablePid = $latestProcess.Id
        $stableSince = Get-Date
        $gameProcess = $latestProcess
        continue
    }

    $gameProcess = $latestProcess
    if (((Get-Date) - $stableSince).TotalSeconds -ge $StableProcessSeconds) {
        break
    }
}

if (-not $gameProcess -or -not (Get-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue)) {
    throw ("{0}.exe did not stay alive long enough to attach. Increase -StableProcessSeconds or inspect the game startup path." -f $GameProcessName)
}

$session = @{
    launchedAt = (Get-Date).ToString('o')
    mode = $Mode
    gamePid = $gameProcess.Id
    gameProcess = $gameProcess.ProcessName
    mo2Executable = $ExecutableName
    modsRoot = $modsRoot
    pluginModPath = $pluginModPath
}
$session | ConvertTo-Json | Set-Content $stateFile
@(
    ('gamePid={0}' -f $gameProcess.Id)
    ('gameProcess={0}' -f $gameProcess.ProcessName)
    ('mode={0}' -f $Mode)
    ('launchedAt={0}' -f $session.launchedAt)
) | Set-Content $stateEnvFile

Write-Status ("[starfield-debug] {0}.exe pid: {1}" -f $gameProcess.ProcessName, $gameProcess.Id)

if ($EmitPidOnly) {
    Write-Output $gameProcess.Id
    return
}

if ($SkipAttach) {
    Write-Status '[starfield-debug] skipping debugger attach'
    return
}

if (-not (Test-Path $vsJitDebugger)) {
    throw ("Visual Studio JIT debugger not found: {0}" -f $vsJitDebugger)
}

Write-Status ("[starfield-debug] attaching Visual Studio JIT debugger to pid {0}" -f $gameProcess.Id)
Start-Process -FilePath $vsJitDebugger -ArgumentList @('-p', $gameProcess.Id.ToString()) | Out-Null
