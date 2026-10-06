param(
    [string] $BaselineRef = 'codex/cpu-microbench-baseline',
    [string] $OutputDirectory = 'build/microbench-comparison',
    [string] $Compiler = 'gcc'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$baselineRoot = Join-Path $outputRoot 'baseline-source'
if (Test-Path -LiteralPath $baselineRoot) {
    throw "Snapshot already exists: $baselineRoot. Choose a fresh output directory."
}
$baselineCommit = (& git -C $repoRoot rev-parse "$BaselineRef^{commit}").Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve baseline revision' }
$currentCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
New-Item -ItemType Directory -Path $baselineRoot | Out-Null
$archive = Join-Path $outputRoot 'baseline-source.tar'
& git -C $repoRoot archive --format=tar "--output=$archive" $baselineCommit src tests/cpu
if ($LASTEXITCODE -ne 0) { throw 'Cannot export baseline sources' }
& tar -xf $archive -C $baselineRoot
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract baseline sources' }
# Backport only the fixture. The baseline emulator sources stay at their commit.
Copy-Item -LiteralPath (Join-Path $repoRoot 'tests/cpu/cpu_microbench.c') -Destination (Join-Path $baselineRoot 'tests/cpu/cpu_microbench.c')
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$env:PATH = (Split-Path $compilerPath) + [IO.Path]::PathSeparator + $env:PATH
$commonFlags = @('-O2', '-g', '-DNDEBUG', '-std=gnu11', '-fomit-frame-pointer', '-fno-strict-aliasing',
    '-m64', '-march=x86-64', '-msse2', '-mfpmath=sse', '-mstackrealign', '-ffunction-sections',
    '-fdata-sections', '-fno-asynchronous-unwind-tables', '-DUSE_DYNAREC', '-DUSE_NEW_DYNAREC', '-Wl,--gc-sections')
$sources = @('tests/cpu/cpu_microbench.c', 'src/codegen_new/codegen_ops_jump.c',
    'src/codegen_new/codegen_ops_arith.c', 'src/codegen_new/codegen_ops_setcc.c',
    'src/codegen_new/codegen_ops_mov.c',
    'src/codegen_new/codegen_ops_helpers.c', 'src/codegen_new/codegen_block.c',
    'src/codegen_new/codegen_backend_x86-64_ops.c', 'src/codegen_new/codegen_backend_x86-64_ops_sse.c')
$builds = [ordered] @{}
foreach ($variant in @('baseline', 'current')) {
    $sourceRoot = if ($variant -eq 'baseline') { $baselineRoot } else { $repoRoot }
    $executable = Join-Path $outputRoot "$variant.exe"
    $arguments = $commonFlags + @("-I$sourceRoot/src/include", "-I$sourceRoot/src/cpu")
    $arguments += @($sources | ForEach-Object { Join-Path $sourceRoot $_ })
    $arguments += @('-o', $executable)
    & $compilerPath @arguments
    if ($LASTEXITCODE -ne 0) { throw "$variant compilation failed" }
    $builds[$variant] = @{ executable = $executable; arguments = $arguments
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash }
}
@{
    baseline_ref = $BaselineRef; baseline_commit = $baselineCommit; current_commit = $currentCommit
    compiler = $compilerPath; compiler_version = (& $compilerPath --version | Select-Object -First 1)
    harness_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repoRoot 'tests/cpu/cpu_microbench.c')).Hash
    source_diff = (& git -C $repoRoot diff HEAD -- src/codegen_new src/cpu)
    builds = $builds
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'build.json') -Encoding utf8
Write-Output "Built identical harnesses against $baselineCommit and $currentCommit in $outputRoot"
