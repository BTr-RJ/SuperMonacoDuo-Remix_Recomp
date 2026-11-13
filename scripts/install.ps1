param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$RomFolder1='', [string]$RomFolder2='',
    [string]$RomPathsFile='', [string]$BuildFolder='build/native20', [switch]$SkipBuild, [switch]$NoToolInstall
)
# BuildFolder is a stable install location (tools/update_release.py BUILD must match).
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
$OutputEncoding=[Console]::OutputEncoding
$version='0.60'
$packageRoot=Split-Path -Parent $PSScriptRoot
$installRoot=[IO.Path]::GetFullPath($Destination)
$sourceRoot=[IO.Path]::GetFullPath($packageRoot)
if ($installRoot -eq [IO.Path]::GetPathRoot($installRoot)) { throw 'Choose a dedicated installation folder, not a drive root.' }
if ($installRoot.StartsWith($sourceRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose an installation folder outside the installer package.' }
$marker=Join-Path $installRoot '.local/installation.json'
if (Test-Path -LiteralPath $installRoot) {
    $contents=@(Get-ChildItem -LiteralPath $installRoot -Force)
    # Setting up inside the package folder itself (RecompLauncher) is the one allowed non-empty case.
    if ($contents.Count -gt 0 -and $installRoot -ne $sourceRoot) {
        if (-not (Test-Path -LiteralPath $marker)) { throw 'The destination is not empty. Choose a new empty folder or an existing Super Monaco Duo-Remix Recomp installation.' }
        $installed=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        if ($installed.product -ne 'SuperMonacoDuoRecomp') { throw 'This folder belongs to a different product.' }
    }
}
$romFolders=@($RomFolder1,$RomFolder2) | Where-Object { $_ }
if ($RomPathsFile) { $romFolders=@(Get-Content -LiteralPath $RomPathsFile -Encoding UTF8 | Where-Object { $_ }) }
if ($romFolders.Count -eq 0) { throw 'Select at least one folder containing your own compatible ROMs.' }
foreach ($folder in $romFolders) { if (-not (Test-Path -LiteralPath $folder)) { throw "ROM folder not found: $folder" } }
Write-Output '[STAGE 1/7] Preparing installation folder'
New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $installRoot '.local') -Force | Out-Null
@{product='SuperMonacoDuoRecomp';version=$version;status='installing'} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
if ($sourceRoot -ne $installRoot) {
    foreach ($name in @('src','tools','scripts','assets','docs')) {
        $from=Join-Path $sourceRoot $name
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination $installRoot -Recurse -Force }
    }
    foreach ($name in @('CMakeLists.txt','SuperMonacoDuoRecomp.exe','SDL2.dll','README.md','CREDITS.md','CONTRIBUTING.md','THIRD_PARTY_NOTICES.md','Jogar.cmd','recomplauncher.json')) {
        $from=Join-Path $sourceRoot $name
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination $installRoot -Force }
    }
}
Write-Output '[STAGE 2/7] Verifying the packaged build tools'
# The release packages python, cmake, ninja, the patched engine and librashader under .tools,
# each file listed with SHA-256 in PACKAGE-MANIFEST.json (there is no download lock file).
$tools=Join-Path $installRoot '.tools'
if ($sourceRoot -ne $installRoot) {
    $manifestFile=Join-Path $sourceRoot 'PACKAGE-MANIFEST.json'
    if (Test-Path -LiteralPath $manifestFile) {
        $manifest=Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
        foreach ($entry in ($manifest.files | Where-Object { $_.path -like '.tools/*' })) {
            $file=Join-Path $sourceRoot $entry.path
            if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Checksum mismatch in packaged tool: $($entry.path). Extract the complete release again." }
        }
    }
    foreach ($name in @('python','cmake','ninja','engine','librashader')) {
        $from=Join-Path $sourceRoot ".tools/$name"
        if (Test-Path -LiteralPath $from) { New-Item -ItemType Directory -Path $tools -Force | Out-Null; Copy-Item -LiteralPath $from -Destination $tools -Recurse -Force }
    }
}
$python=Join-Path $tools 'python/python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Packaged Python was not found. Extract the complete release ZIP.' }
Write-Output '[STAGE 3/7] Finding and verifying your ROM revisions'
$arguments=@((Join-Path $installRoot 'tools/install_roms.py'))
foreach ($folder in $romFolders) { $arguments+=@('--rom-dir',$folder) }
& $python @arguments
if ($LASTEXITCODE -ne 0) { throw 'The two qualified ROMs were not staged. See docs/INSTALLATION.md.' }
if ($SkipBuild) { Write-Output '[STAGE 7/7] ROM validation complete (build intentionally skipped)'; exit 0 }
Write-Output '[STAGE 4/7] Checking packaged SDK files'
foreach ($rel in @('cmake/cmake-4.4.5-windows-x86_64/bin/cmake.exe','ninja/ninja.exe','engine/runner/main.c','librashader/librashader.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $tools $rel))) { throw "Missing packaged tool: $rel" }
}
Write-Output '[STAGE 5/7] Checking the Microsoft C++ toolchain'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vcRoot=$null
if (Test-Path -LiteralPath $vswhere) { $vcRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
if (-not $vcRoot) {
    $fallback=Get-ChildItem -Path "${env:ProgramFiles(x86)}/Microsoft Visual Studio/2022/*/VC/Auxiliary/Build/vcvars64.bat" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $fallback) {
        if ($NoToolInstall) { throw 'Microsoft C++ Build Tools are required; automatic tool installation was disabled.' }
        Write-Output 'Microsoft C++ Build Tools are missing. Windows may request administrator approval.'
        $cache=Join-Path $tools 'downloads'; New-Item -ItemType Directory -Path $cache -Force | Out-Null
        $vsInstaller=Join-Path $cache 'vs_BuildTools.exe'
        [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $vsInstaller
        $signature=Get-AuthenticodeSignature -LiteralPath $vsInstaller
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') { throw 'The Microsoft installer signature could not be verified.' }
        $setup=Start-Process -FilePath $vsInstaller -ArgumentList '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if ($setup.ExitCode -notin @(0,3010)) { throw "Microsoft Build Tools installation failed ($($setup.ExitCode))." }
    }
}
Write-Output '[STAGE 6/7] Generating and compiling the games locally'
# Visual Studio's vcvars prints harmless vswhere notices on stderr; only the exit code decides.
$previousPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $installRoot 'scripts/build.ps1') -BuildFolder $BuildFolder
$buildExit=$LASTEXITCODE;$ErrorActionPreference=$previousPreference
$LASTEXITCODE=$buildExit
if ($LASTEXITCODE -ne 0) { throw 'Local compilation failed. Your original ROM folders were not modified. See the installation log.' }
Write-Output '[STAGE 7/7] Finalizing installation'
$installedGames=@()
foreach ($game in @('smgp1','smgp2')) { if (Test-Path -LiteralPath (Join-Path $installRoot "$BuildFolder/bin/$game/$game.exe")) { $installedGames+=$game } }
if ($installedGames.Count -ne 2) { throw 'Both game executables are required; compilation did not produce them.' }
@{product='SuperMonacoDuoRecomp';version=$version;games=$installedGames;installed_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
if (Test-Path -LiteralPath (Join-Path $installRoot 'SuperMonacoDuoRecomp.exe')) {
    $link=(New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $installRoot 'Super Monaco Duo-Remix Recomp.lnk'))
    $link.TargetPath=Join-Path $installRoot 'SuperMonacoDuoRecomp.exe';$link.WorkingDirectory=$installRoot;$link.Save()
}
Write-Output "Installation complete. Games ready: $($installedGames -join ', ')."
