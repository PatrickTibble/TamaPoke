# Rebuilds everything web/index.html installs: the four firmware parts and manifest.json.
# Windows counterpart of tools/build_web.sh (which assumes macOS paths).
#
#   powershell -ExecutionPolicy Bypass -File tools\build_web.ps1            # firmware only
#   powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -Sprites   # also repack sprites-<region>.pak
#   powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -SkipCheck # skip check_installer.py (not advised)
#   powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -Serve     # build, then serve web\ and open the installer
#   powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -ServeOnly # just serve web\ (no rebuild)
param([switch]$Sprites, [switch]$SkipCheck, [switch]$Serve, [switch]$ServeOnly, [int]$Port = 8000)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$FQBN = 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB'
$B = 'build\esp32.esp32.esp32s3'
$FW = 'web\firmware'

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# --- python, for the save-safety check, the sprite packer and the local server
$py = $null; $pyArgs = @()
if (Get-Command py -ErrorAction SilentlyContinue) { $py = 'py'; $pyArgs = @('-3') }
elseif (Get-Command python -ErrorAction SilentlyContinue) { $py = 'python' }

# The page fetches manifest.json relative to itself, so the server's root MUST be
# web\. It is passed as an absolute --directory: an admin console opens in
# System32, where a "cd web" fails and the server would serve the wrong folder.
function Serve-Web {
  if (-not $py) { Fail 'Python not found (py or python); it serves web\ for the installer.' }
  $web = Join-Path $Root 'web'
  if (-not (Test-Path (Join-Path $web 'manifest.json'))) { Fail "no manifest.json in $web" }
  $url = "http://localhost:$Port/"
  Write-Host "Serving $web at $url (Ctrl+C to stop). Web Serial needs localhost or HTTPS." -ForegroundColor Green
  Start-Job { Start-Sleep -Seconds 1; Start-Process $using:url } | Out-Null
  & $py @pyArgs -m http.server $Port --bind localhost --directory $web
  exit $LASTEXITCODE
}
if ($ServeOnly) { Serve-Web }
function Run($exe, [string[]]$argv, $what) {
  & $exe @argv
  if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit $LASTEXITCODE)" }
}
function FirstCmd([string[]]$names) {
  foreach ($n in $names) { $c = Get-Command $n -ErrorAction SilentlyContinue | Select-Object -First 1; if ($c) { return $c.Source } }
  return $null
}

# --- arduino-cli: PATH first, then the copy bundled with the Arduino IDE
$ideCli = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
$cli = FirstCmd @('arduino-cli')
if (-not $cli -and (Test-Path $ideCli)) { $cli = $ideCli }
if (-not $cli) { Fail "arduino-cli not found on PATH or at $ideCli" }
Write-Host "arduino-cli: $cli"

Write-Host 'Compiling...'
Run $cli @('compile', '--fqbn', $FQBN, '--export-binaries', '.') 'arduino-cli compile'

# --- THE FOUR PARTS ARE WHAT THE INSTALLER SHIPS. A single merged image at offset 0
# pads its gaps with 0xFF and so writes blank flash over NVS at 0x9000 -- the
# player's save. Parts at their own offsets leave 0x9000..0xE000 alone.
New-Item -ItemType Directory -Force $FW | Out-Null
$parts = [ordered]@{
  'TamaPoke.ino.bootloader.bin' = 'bootloader.bin'
  'TamaPoke.ino.partitions.bin' = 'partitions.bin'
  'boot_app0.bin'               = 'boot_app0.bin'
  'TamaPoke.ino.bin'            = 'app.bin'
}
foreach ($src in $parts.Keys) {
  $p = Join-Path $B $src
  if (-not (Test-Path $p)) { Fail "missing build output $p" }
  Copy-Item $p (Join-Path $FW $parts[$src]) -Force
}
Write-Host "OK -> $FW (4 parts)"

# --- the manifest version is what the installer shows; it always comes from FW_VERSION
$m = Select-String -Path 'TamaPoke.ino' -Pattern '^\s*#define\s+FW_VERSION\s+"([^"]+)"' | Select-Object -First 1
if (-not $m) { Fail 'could not read #define FW_VERSION from TamaPoke.ino' }
$ver = $m.Matches[0].Groups[1].Value

# new_install_prompt_erase is ALWAYS true, and the name is the opposite of what it
# does: in esp-web-tools' no-Improv path (ours) FALSE means "do not ask, erase the
# whole chip", NVS included, whatever parts are listed. TRUE shows an "Erase device"
# checkbox that starts unchecked. Setting it false destroyed two real saves.
# The offsets are 0 / 0x8000 / 0xE000 / 0x10000; nothing may land on 0x9000..0xE000.
$manifest = @"
{
  "name": "TamaPoke",
  "version": "$ver",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        {
          "path": "firmware/bootloader.bin",
          "offset": 0
        },
        {
          "path": "firmware/partitions.bin",
          "offset": 32768
        },
        {
          "path": "firmware/boot_app0.bin",
          "offset": 57344
        },
        {
          "path": "firmware/app.bin",
          "offset": 65536
        }
      ]
    }
  ]
}
"@
[IO.File]::WriteAllText((Join-Path (Get-Location) 'web\manifest.json'), ($manifest -replace "`r`n", "`n"), (New-Object Text.UTF8Encoding $false))
Write-Host "manifest version -> $ver"

# --- merged image for flashing a BLANK board from the command line only. It is
# deliberately NOT in the manifest: it would erase the save.
$esptool = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py\*\esptool.exe') -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $esptool) { $esptool = FirstCmd @('esptool', 'esptool.py') }
if ($esptool) {
  $merge = @('--chip', 'esp32s3', '', '-o', "$FW\tamapoke.bin",
    '0x0', "$B\TamaPoke.ino.bootloader.bin", '0x8000', "$B\TamaPoke.ino.partitions.bin",
    '0xe000', "$B\boot_app0.bin", '0x10000', "$B\TamaPoke.ino.bin")
  $ok = $false
  foreach ($verb in @('merge-bin', 'merge_bin')) {
    $merge[2] = $verb
    & $esptool @merge
    if ($LASTEXITCODE -eq 0) { $ok = $true; break }
  }
  if ($ok) { Write-Host "OK -> $FW\tamapoke.bin (blank boards only)" }
  else { Write-Warning "esptool merge failed; $FW\tamapoke.bin is stale (the installer does not use it)" }
} else {
  Write-Warning "esptool not found; $FW\tamapoke.bin not rebuilt (the installer does not use it)"
}

if ($SkipCheck) {
  Write-Warning 'check_installer.py skipped (-SkipCheck)'
} else {
  if (-not $py) { Fail 'Python not found (py or python); it runs check_installer.py, which keeps the installer from wiping saves. Install Python or pass -SkipCheck.' }
  Write-Host 'Checking the installer cannot erase a save...'
  Run $py ($pyArgs + @('tools\check_installer.py')) 'check_installer.py (installer would wipe saves -- refusing)'
}

if ($Sprites) {
  if (-not $py) { Fail 'Python not found; -Sprites needs it for tools\pack_bundle.py' }
  Write-Host 'Packing sprites...'
  Run $py ($pyArgs + @('tools\pack_bundle.py')) 'pack_bundle.py'
}

Write-Host ''
Write-Host "Built TamaPoke v$ver for web\index.html." -ForegroundColor Green
if ($Serve) { Serve-Web }
Write-Host 'To install from it, serve web\ (Web Serial needs localhost or HTTPS):'
Write-Host '  powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -ServeOnly'
