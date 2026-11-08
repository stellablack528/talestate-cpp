param(
    [string]$BuildDir = "build",
    [string]$QtBin = "E:\QT\6.10.1\mingw_64\bin",
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "版本号格式无效。" }
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = (Resolve-Path (Join-Path $root $BuildDir)).Path
$exe = Join-Path $build "talestate_desktop.exe"
$deploy = Join-Path $QtBin "windeployqt.exe"
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $deploy)) {
    throw "请先构建桌面程序，并提供正确的 Qt bin 目录。"
}
$env:Path = "$QtBin;$env:Path"
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$dist = (Resolve-Path -LiteralPath $dist).Path
$out = Join-Path $dist "TaleState-$Version-win64"
if (!$out.StartsWith($dist + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) { throw "打包目录超出项目范围。" }
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $out "TaleState.exe")
& $deploy --release --compiler-runtime --no-translations (Join-Path $out "TaleState.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt 依赖打包失败。" }
Copy-Item -LiteralPath (Join-Path $root "examples") -Destination (Join-Path $out "examples") -Recurse
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination $out
Copy-Item -LiteralPath (Join-Path $root "RELEASE-v0.1.0.md") -Destination (Join-Path $out "使用说明.md")
$archive = Join-Path $root "dist\TaleState-$Version-win64.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -LiteralPath $out -DestinationPath $archive
Write-Output $archive
