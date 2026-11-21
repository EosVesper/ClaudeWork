# 编译统一工程的燃烧室、喷管和产物补充物性模块。
# 在本目录运行：powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
$ErrorActionPreference = 'Stop'
$savedPath = $env:PATH
$compilerDirs = @('C:\msys64\ucrt64\bin',
                  'D:\program_station\QingShiLu\msys64\ucrt64\bin',
                  'D:\program_station\msys64\ucrt64\bin')
$gcc = 'gcc'
$gxx = 'g++'
foreach ($dir in $compilerDirs) {
    if ((Test-Path -LiteralPath (Join-Path $dir 'gcc.exe')) -and
        (Test-Path -LiteralPath (Join-Path $dir 'g++.exe'))) {
        $gcc = Join-Path $dir 'gcc.exe'
        $gxx = Join-Path $dir 'g++.exe'
        $env:PATH = "$dir;$savedPath"
        break
    }
}
if (-not (Get-Command $gcc -ErrorAction SilentlyContinue) -or
    -not (Get-Command $gxx -ErrorAction SilentlyContinue)) {
    throw '未找到 GCC/G++ 编译器。'
}

Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    $include = @('-Isrc/chamber', '-Isrc/nozzle', '-Isrc/products', '-Isrc/presets')
    $objects = @()
    $cFiles = @(Get-ChildItem -LiteralPath 'src/chamber','src/nozzle','src/products','src/presets' -Filter '*.c' -File)
    foreach ($source in $cFiles) {
        $object = "build/$($source.BaseName).o"
        & $gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 @include `
            -c $source.FullName -o $object
        if ($LASTEXITCODE -ne 0) { throw "编译失败：$($source.Name)" }
        $objects += $object
    }
    & $gxx -std=c++11 -Wall -Wextra -Wpedantic -Werror -O2 -DTHERMO_NO_MAIN `
        @include -c 'src/chamber/thermo_properties.cpp' -o 'build/thermo_properties.o'
    if ($LASTEXITCODE -ne 0) { throw '编译失败：thermo_properties.cpp' }
    $objects += 'build/thermo_properties.o'

    foreach ($app in @('liquid_chamber','solid_chamber','nozzle_cli')) {
        & $gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 @include `
            -c "apps/$app.c" -o "build/$app.o"
        if ($LASTEXITCODE -ne 0) { throw "编译失败：$app.c" }
    }
    & $gxx -static @objects 'build/liquid_chamber.o' -o 'liquid_chamber.exe'
    if ($LASTEXITCODE -ne 0) { throw '链接失败：liquid_chamber.exe' }
    & $gxx -static @objects 'build/solid_chamber.o' -o 'solid_chamber.exe'
    if ($LASTEXITCODE -ne 0) { throw '链接失败：solid_chamber.exe' }
    & $gxx -static @objects 'build/nozzle_cli.o' -o 'rocket_thermo.exe'
    if ($LASTEXITCODE -ne 0) { throw '链接失败：rocket_thermo.exe' }
    Write-Output '构建完成：liquid_chamber.exe、solid_chamber.exe、rocket_thermo.exe'
} finally {
    Pop-Location
    $env:PATH = $savedPath
}
