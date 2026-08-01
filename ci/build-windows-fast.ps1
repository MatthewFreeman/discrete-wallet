param(
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
  [string]$Configuration = 'Release',
  [ValidateRange(1, 256)]
  [int]$Jobs = [Environment]::ProcessorCount,
  [string]$QtRoot = 'C:\Qt\6.9.2\msvc2022_64',
  [string]$BoostRoot = 'C:\thirdparties\boost-1.86.0'
)

$ErrorActionPreference = 'Stop'

$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoParent = Split-Path $repoPath -Parent
$repoName = Split-Path $repoPath -Leaf
$buildDrive = 'W:'
$createdDrive = $false
$developerShell = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1'

if (-not (Test-Path -LiteralPath $developerShell)) {
  throw "Visual Studio 2022 Build Tools developer shell not found: $developerShell"
}
if (-not (Test-Path -LiteralPath $QtRoot)) {
  throw "Qt installation not found: $QtRoot"
}
if (-not (Test-Path -LiteralPath $BoostRoot)) {
  throw "Boost installation not found: $BoostRoot"
}

$mapping = (& subst.exe) | Where-Object { $_ -like "$buildDrive\:*" }
if ($mapping) {
  $mappedPath = ($mapping -split '=>', 2)[1].Trim()
  if ([IO.Path]::GetFullPath($mappedPath) -ne [IO.Path]::GetFullPath($repoParent)) {
    throw "$buildDrive is already mapped to '$mappedPath'; expected '$repoParent'."
  }
} else {
  & subst.exe $buildDrive $repoParent
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to map $buildDrive to '$repoParent'."
  }
  $createdDrive = $true
}

try {
  & $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

  $sourcePath = "$buildDrive\$repoName"
  $buildPath = "$buildDrive\b"
  $env:CMAKE_BUILD_PARALLEL_LEVEL = $Jobs.ToString()
  $env:CMAKE_GENERATOR = 'Ninja Multi-Config'
  $env:CMAKE_PREFIX_PATH = "$QtRoot\lib\cmake"

  Write-Host "Building $Configuration with $Jobs parallel jobs."
  & cmake -S $sourcePath -B $buildPath `
    -G 'Ninja Multi-Config' `
    "-DBOOST_ROOT=$BoostRoot" `
    "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH" `
    -DARCH=default
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  & cmake --build $buildPath `
    --config $Configuration `
    --parallel $Jobs `
    --target DiscreteWallet
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  Write-Host "Built: $buildPath\$Configuration\DiscreteWallet.exe"
} finally {
  if ($createdDrive) {
    & subst.exe $buildDrive /D
  }
}
