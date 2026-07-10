[CmdletBinding()]
param(
    [string]$ProjectFile,
    [string]$EngineRoot,
    [ValidateRange(1, 65535)]
    [int]$Port = 8000,
    [string]$UrlPath = "/mcp",
    [switch]$LaunchEditor
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectFile {
    param([string]$ExplicitProjectFile)

    if ($ExplicitProjectFile) {
        return (Resolve-Path -LiteralPath $ExplicitProjectFile).Path
    }

    $DefaultProject = Join-Path $PSScriptRoot "..\..\..\Winyunq.uproject"
    if (Test-Path -LiteralPath $DefaultProject) {
        return (Resolve-Path -LiteralPath $DefaultProject).Path
    }

    $Candidates = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot "..\..\..") -Filter *.uproject -File
    if ($Candidates.Count -ne 1) {
        throw "Pass -ProjectFile. Could not identify exactly one .uproject file."
    }
    return $Candidates[0].FullName
}

function Resolve-EngineRoot {
    param(
        [string]$ExplicitEngineRoot,
        [string]$EngineAssociation
    )

    $Candidates = [System.Collections.Generic.List[string]]::new()
    if ($ExplicitEngineRoot) { $Candidates.Add($ExplicitEngineRoot) }
    if ($env:UE_ENGINE_ROOT) { $Candidates.Add($env:UE_ENGINE_ROOT) }

    $LauncherManifest = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path -LiteralPath $LauncherManifest) {
        $LauncherData = Get-Content -LiteralPath $LauncherManifest -Raw | ConvertFrom-Json
        foreach ($Install in $LauncherData.InstallationList) {
            if ($Install.AppName -eq "UE_$EngineAssociation") {
                $Candidates.Add([string]$Install.InstallLocation)
            }
        }
    }

    $Candidates.Add("D:\UE_$EngineAssociation")
    $Candidates.Add("C:\Program Files\Epic Games\UE_$EngineAssociation")

    foreach ($Candidate in $Candidates) {
        if (-not $Candidate) { continue }
        $Descriptor = Join-Path $Candidate "Engine\Plugins\Experimental\ModelContextProtocol\ModelContextProtocol.uplugin"
        if (Test-Path -LiteralPath $Descriptor) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    throw "Could not locate the UE $EngineAssociation installation. Pass -EngineRoot."
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Text)
    $Encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $Text, $Encoding)
}

function Set-McpEditorSettings {
    param([string]$ConfigPath, [int]$ServerPort, [string]$ServerUrlPath)

    $SectionName = "/Script/ModelContextProtocolEngine.ModelContextProtocolSettings"
    $Section = @"
[$SectionName]
ServerUrlPath=$ServerUrlPath
ServerPortNumber=$ServerPort
bAutoStartServer=True
bEnableToolSearch=True
"@

    $Existing = if (Test-Path -LiteralPath $ConfigPath) {
        [System.IO.File]::ReadAllText($ConfigPath)
    } else {
        ""
    }

    $Pattern = "(?ms)^\[" + [regex]::Escape($SectionName) + "\]\r?\n.*?(?=^\[|\z)"
    if ([regex]::IsMatch($Existing, $Pattern)) {
        $Updated = [regex]::Replace($Existing, $Pattern, $Section.TrimEnd() + "`r`n")
    } else {
        $Updated = $Existing.TrimEnd() + $(if ($Existing.Trim()) { "`r`n`r`n" } else { "" }) + $Section.TrimEnd() + "`r`n"
    }

    $ConfigDirectory = Split-Path -Parent $ConfigPath
    if (-not (Test-Path -LiteralPath $ConfigDirectory)) {
        New-Item -ItemType Directory -Path $ConfigDirectory | Out-Null
    }
    Write-Utf8NoBom -Path $ConfigPath -Text $Updated
}

$ProjectFile = Resolve-ProjectFile -ExplicitProjectFile $ProjectFile
$Project = Get-Content -LiteralPath $ProjectFile -Raw | ConvertFrom-Json
$EngineRoot = Resolve-EngineRoot -ExplicitEngineRoot $EngineRoot -EngineAssociation ([string]$Project.EngineAssociation)

$McpDescriptor = Join-Path $EngineRoot "Engine\Plugins\Experimental\ModelContextProtocol\ModelContextProtocol.uplugin"
$RegistryDescriptor = Join-Path $EngineRoot "Engine\Plugins\Experimental\ToolsetRegistry\ToolsetRegistry.uplugin"
$ToolsetsRoot = Join-Path $EngineRoot "Engine\Plugins\Experimental\Toolsets"

$Descriptors = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
$Descriptors.Add((Get-Item -LiteralPath $McpDescriptor))
$Descriptors.Add((Get-Item -LiteralPath $RegistryDescriptor))
Get-ChildItem -LiteralPath $ToolsetsRoot -Recurse -Filter *.uplugin -File | ForEach-Object { $Descriptors.Add($_) }

$PluginNames = $Descriptors | ForEach-Object { $_.BaseName } | Sort-Object -Unique
$Plugins = [System.Collections.Generic.List[object]]::new()
foreach ($Plugin in @($Project.Plugins)) { $Plugins.Add($Plugin) }

foreach ($PluginName in $PluginNames) {
    $ExistingPlugin = $Plugins | Where-Object { $_.Name -eq $PluginName } | Select-Object -First 1
    if ($ExistingPlugin) {
        if ($ExistingPlugin.PSObject.Properties.Name -contains "Enabled") {
            $ExistingPlugin.Enabled = $true
        } else {
            $ExistingPlugin | Add-Member -NotePropertyName Enabled -NotePropertyValue $true
        }
    } else {
        $Plugins.Add([pscustomobject]@{ Name = $PluginName; Enabled = $true })
    }
}

$Project.Plugins = @($Plugins)
$BackupPath = "$ProjectFile.pre-ue58-mcp.bak"
if (-not (Test-Path -LiteralPath $BackupPath)) {
    Copy-Item -LiteralPath $ProjectFile -Destination $BackupPath
}

$ProjectJson = $Project | ConvertTo-Json -Depth 100
Write-Utf8NoBom -Path $ProjectFile -Text ($ProjectJson + "`r`n")

$ProjectRoot = Split-Path -Parent $ProjectFile
$SettingsPath = Join-Path $ProjectRoot "Config\DefaultEditorPerProjectUserSettings.ini"
Set-McpEditorSettings -ConfigPath $SettingsPath -ServerPort $Port -ServerUrlPath $UrlPath

Write-Host "Enabled UE 5.8 built-in MCP server and $($PluginNames.Count - 2) Toolset plugins."
Write-Host "Project: $ProjectFile"
Write-Host "Engine:  $EngineRoot"
Write-Host "Server:  http://127.0.0.1:$Port$UrlPath"
Write-Host "Plugins: $($PluginNames -join ', ')"

if ($LaunchEditor) {
    $EditorExe = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    $RunningEditor = Get-Process UnrealEditor -ErrorAction SilentlyContinue
    if ($RunningEditor) {
        Write-Warning "Unreal Editor is already running. Close it and rerun with -LaunchEditor, or restart it manually."
    } else {
        Start-Process -FilePath $EditorExe -ArgumentList @(
            $ProjectFile,
            "-ModelContextProtocolStartServer",
            "-ModelContextProtocolPort=$Port"
        )
    }
}
