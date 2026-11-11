# katamari-nx: one emulator run of the NRO in the user's patched Ryubing with this port's own data folder
# (docs/porting-start-here.md of banjo-nx: "Build and test essentials"). Started minimised; refuses to start while any
# Ryujinx runs or when the free commit memory is under -MinCommitGB; stops only the process it started.
#   powershell -File tools/run_ryujinx.ps1 -Seconds 300 -Name emu1 [-Toml <a blur.toml with test keys>]
# renut-ultimate (2026-11-07): its own data folder (ryu-data\renut-ultimate, made from katamari-nx's), and the NRO of
# app\out\<-Build> copied to its SD at each run (katamari-nx's folder and NRO are left alone).
# blur-nx (2026-11-10, the request): this port's own data folder (ryu-data\blur: katamari's system folders, keys and
# firmware, not its sdcard), the SD's /switch/blur-nx, at most two Ryujinx runs across all ports (start only when fewer
# than two run), 6 GB of free commit memory or more.
# blur-nx (2026-11-11): the run's folder on C: (-OutRoot; the request: big test outputs on C:), with the ELF of the NRO it
# ran (elf\renut, its md5 and the NRO's in build.txt): a profile read with another build's ELF names the wrong functions,
# and emu6-emu11's ELFs were overwritten by later builds.
param([int]$Seconds = 240, [string]$Name = "emu", [string]$Toml = "", [double]$MinCommitGB = 6, [string]$Build = "switch",
      [string]$OutRoot = "C:\blur-nx-test\emu_runs")
$root = Split-Path -Parent $PSScriptRoot
$ryu = "D:\New folder\Ryubing-src\ryubing\build\Ryujinx.exe"
$data = "D:\New folder\ryu-data\blur"
# Ryujinx starts an NRO without argv: the app then takes REX_SWITCH_FALLBACK_EXE (app/CMakeLists.txt) as its own path.
$sd = Join-Path $data "sdcard\switch\blur-nx"
$nro = Join-Path $sd "blur-nx.nro"
$out = Join-Path $OutRoot $Name
if (Test-Path $out) { Write-Output "$out exists: give the run another -Name"; exit 5 }
$running = @(Get-Process -Name Ryujinx -ErrorAction SilentlyContinue).Count
if ($running -ge 2) { Write-Output "$running Ryujinx runs already (other ports'): not starting"; exit 2 }
$os = Get-CimInstance Win32_OperatingSystem
$freeCommit = ($os.TotalVirtualMemorySize - ($os.TotalVirtualMemorySize - $os.FreeVirtualMemory)) / 1MB
if ($freeCommit -lt $MinCommitGB) { Write-Output ("free commit memory {0:N1} GB < {1} GB: not starting" -f $freeCommit, $MinCommitGB); exit 3 }
$built = Join-Path $root "app\out\$Build\renut.nro"
if (-not (Test-Path $built)) { Write-Output "no NRO at $built"; exit 4 }
Copy-Item $built $nro -Force
# blur-nx (2026-11-10): the test builds (BLUR_BUILTIN_CONFIG=OFF) read renut.toml, the player's build blur.toml: both.
if ($Toml) {
  Copy-Item $Toml (Join-Path $sd "renut.toml") -Force
  Copy-Item $Toml (Join-Path $sd "blur.toml") -Force
}
New-Item -ItemType Directory -Force $out | Out-Null
$elf = Join-Path $root "app\out\$Build\renut"
$info = @("nro $built", "nro md5 $((Get-FileHash -Algorithm MD5 $built).Hash.ToLower())")
if (Test-Path $elf) {
  New-Item -ItemType Directory -Force (Join-Path $out "elf") | Out-Null
  Copy-Item $elf (Join-Path $out "elf\renut") -Force
  $info += "elf md5 $((Get-FileHash -Algorithm MD5 $elf).Hash.ToLower())"
} else { $info += "no ELF at $elf (a profile of this run cannot be read)" }
if ($Toml) { Copy-Item $Toml (Join-Path $out "run.toml") -Force; $info += "toml $Toml" }
$info | Set-Content -Encoding utf8 (Join-Path $out "build.txt")
$logs = Join-Path $sd "logs"
if (Test-Path $logs) { Remove-Item -Recurse -Force $logs }
$env:DOTNET_ROOT = "D:\New folder\dotnet10"
# katamari-nx (2026-10-31): the run's own start time ($p.StartTime filtered nothing: every run copied every capture
# the SD had ever kept, ~800 MB a run, 19 GB in out/emu_runs).
$started = Get-Date
$p = Start-Process -FilePath $ryu -ArgumentList @("-r", "`"$data`"", "`"$nro`"") -PassThru -WindowStyle Minimized `
     -RedirectStandardOutput (Join-Path $out "ryujinx_stdout.txt") -RedirectStandardError (Join-Path $out "ryujinx_stderr.txt")
Write-Output ("started Ryujinx pid {0} (free commit {1:N1} GB)" -f $p.Id, $freeCommit)
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Seconds 3 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped pid $($p.Id) after $Seconds s" }
else { Write-Output "Ryujinx exited by itself (code $($p.ExitCode))" }
Start-Sleep -Seconds 2
if (Test-Path $logs) { Copy-Item -Recurse $logs (Join-Path $out "logs") -Force }
# blur-nx (2026-11-10): in Ryujinx the NRO's working folder is the SD's /switch, so the sampler's rex_profile.log (and
# banjo_profiler.log) land there, in this port's own data folder: moved into the run's folder (the next run starts a new
# one).
foreach ($f in @("rex_profile.log", "banjo_profiler.log")) {
  $pf = Join-Path $data "sdcard\switch\$f"
  if ((Test-Path $pf) -and (Get-Item $pf).LastWriteTime -ge $started) { Move-Item $pf (Join-Path $out $f) -Force }
}
# This run's captures move out of the SD (the SD's folder no longer grows); older ones there are left alone.
Get-ChildItem $sd -Filter "*.png" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $started } |
    Move-Item -Destination $out -Force
Get-ChildItem $out -Recurse | Select-Object FullName, Length
