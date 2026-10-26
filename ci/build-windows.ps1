# Builds and tests both Windows VST3s (Knockbox + Knockbox Track).
# Used by both the GitHub Actions workflow and GitLab CI. Output goes to dist\ (plugins) and pluginval-logs\.

$ErrorActionPreference = "Continue"   # every step is checked explicitly below
function Check([string]$what) { if ($LASTEXITCODE -ne 0) { Write-Error "$what failed (exit $LASTEXITCODE)"; exit 1 } }

# 0) Machine info: some CI machines (e.g. GitLab's) are stripped-down Windows Server installs
#    that lack graphics DLLs JUCE loads at startup. Print what is there, for diagnosis.
$nt = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
Write-Output "Windows: $($nt.ProductName) / $($nt.InstallationType) / build $($nt.CurrentBuild)"
foreach ($d in @("dcomp.dll","d2d1.dll","dwrite.dll","d3d11.dll","dxgi.dll","dwmapi.dll","opengl32.dll","glu32.dll","vfw32.dll","comctl32.dll","wininet.dll")) {
  $present = Test-Path "$env:WINDIR\System32\$d"
  Write-Output ("  {0,-14} {1}" -f $d, $(if ($present) { "present" } else { "MISSING" }))
}

# Load the graphics DLLs only when something actually draws ("delay-load"). JUCE's build tool
# (juceaide) and the tests never draw, so they then also run on machines without those DLLs.
# CMake picks this up for every program it links, including juceaide. On a normal PC nothing changes.
$env:LDFLAGS = "/DELAYLOAD:dcomp.dll /DELAYLOAD:d2d1.dll /DELAYLOAD:dwrite.dll /DELAYLOAD:d3d11.dll " +
               "/DELAYLOAD:dxgi.dll /DELAYLOAD:dwmapi.dll /DELAYLOAD:opengl32.dll /DELAYLOAD:glu32.dll " +
               "/DELAYLOAD:vfw32.dll delayimp.lib /IGNORE:4199"

# 1) Find Visual Studio and pick the matching CMake generator
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vsMajor = ((& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion) -split '\.')[0]
Write-Output "Visual Studio $vsMajor at $vsPath"
cmake --version

$gen = switch ($vsMajor) { "16" { "Visual Studio 16 2019" } "17" { "Visual Studio 17 2022" } "18" { "Visual Studio 18 2026" } default { "" } }
if ($gen -ne "") { cmake -B build -G $gen -A x64 -DCMAKE_BUILD_TYPE=Release -DKNOCKBOX_BUILD_SNAPSHOTS=OFF }
else             { cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DKNOCKBOX_BUILD_SNAPSHOTS=OFF }
Check "CMake configure"
cmake --build build --config Release --parallel 4
Check "Build"

# 2) DSP tests (levels, EQ accuracy, clicks, NaNs, block sizes, sample rates)
ctest --test-dir build -C Release --output-on-failure
Check "DSP tests"

$plugins = @(
  @{ Path = "build\Knockbox_artefacts\Release\VST3\Knockbox.vst3";            Name = "Knockbox";       Presets = 16; Ceiling = 1 },
  @{ Path = "build\KnockboxTrack_artefacts\Release\VST3\Knockbox Track.vst3"; Name = "Knockbox Track"; Presets = 16; Ceiling = 0 }
)

# 3) Load each real .vst3 like a DAW does: scan, process, automate, presets, state, editor
foreach ($p in $plugins) {
  & .\build\KnockboxHostTest_artefacts\Release\KnockboxHostTest.exe $p.Path $p.Name $p.Presets $p.Ceiling
  Check "Host test for $($p.Name)"
}

# 4) Must not need the Visual C++ Redistributable (that stops plugins loading on some PCs)
$dumpbin = Get-ChildItem -Recurse -Path "$vsPath\VC\Tools\MSVC" -Filter dumpbin.exe |
           Where-Object { $_.FullName -match "Hostx64\\x64" } | Select-Object -First 1
foreach ($p in $plugins) {
  $dll  = Get-ChildItem -Recurse -LiteralPath $p.Path -Filter *.vst3 -File | Select-Object -First 1
  $deps = & $dumpbin.FullName /DEPENDENTS $dll.FullName | Out-String
  Write-Output $deps
  if ($deps -match "(?i)(MSVCP|VCRUNTIME)[0-9]+.*\.dll") { Write-Error "$($p.Name) depends on the VC++ runtime"; exit 1 }
}

# 5) pluginval (the standard plugin validator) at the strictest level
curl.exe -sSL -o pluginval.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip
Check "pluginval download"
Expand-Archive pluginval.zip -DestinationPath pluginval -Force
foreach ($p in $plugins) {
  & .\pluginval\pluginval.exe --strictness-level 10 --validate-in-process --skip-gui-tests --timeout-ms 900000 --output-dir pluginval-logs $p.Path
  Check "pluginval for $($p.Name)"
}

# 6) Package
New-Item -ItemType Directory -Force -Path dist | Out-Null
foreach ($p in $plugins) { Copy-Item -Recurse -Force -LiteralPath $p.Path dist\ }
Copy-Item INSTALL.txt dist\
Write-Output "All checks passed."
