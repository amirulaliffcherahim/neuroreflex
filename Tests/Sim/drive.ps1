# Drives NeuroReflexSimulator.exe: launches it, posts key messages straight to
# its window (no focus needed), samples pixels to know which screen is up, and
# saves a client-area screenshot per step. Exit code 0 only if every check holds.
# Usage: powershell -File drive.ps1 -OutDir <dir> [-Reaction 260]
param([string]$OutDir = "$PSScriptRoot\shots", [int]$Reaction = 260, [switch]$Full, [int[]]$History = @())

# Optional: seed the simulator's saved history (median ms per past test) so the
# result is judged against a personal normal. Format = Service.cpp's history.bin.
$fsRoot = Resolve-Path "$PSScriptRoot\..\..\Software" | % { Join-Path $_ "Output" }
New-Item -ItemType Directory -Force $fsRoot | Out-Null
Remove-Item "$fsRoot\history.bin" -ErrorAction SilentlyContinue
if ($History.Count) {
    $ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([uint32]0x3152524E); $bw.Write([uint32]$History.Count)
    for ($i = 0; $i -lt 14; $i++) {
        $m = if ($i -lt $History.Count) { $History[$i] } else { 0 }
        $bw.Write([uint16]$m); $bw.Write([byte]0); $bw.Write([byte]$(if ($m) { 3 } else { 0 }))
    }
    [IO.File]::WriteAllBytes("$fsRoot\history.bin", $ms.ToArray())
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public struct R { public int L, T, Rt, B; }
  public struct P { public int X, Y; }
}
"@

$exe = Resolve-Path "$PSScriptRoot\..\..\Software\Apps\LVGL-GUI\simulator\build\bin\NeuroReflexSimulator.exe"
New-Item -ItemType Directory -Force $OutDir | Out-Null
Remove-Item "$OutDir\*.png" -ErrorAction SilentlyContinue
$p = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru -RedirectStandardOutput "$OutDir\sim.log" -RedirectStandardError "$OutDir\sim.err"
$script:fail = @()

function Check([bool]$ok, [string]$what) {
    if ($ok) { "  ok   $what" } else { "  FAIL $what"; $script:fail += $what }
}
function Grab {
    [void][W]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003)   # HWND_TOPMOST, no move/size
    Start-Sleep -Milliseconds 60
    $r = New-Object W+R; [void][W]::GetClientRect($h, [ref]$r)
    $pt = New-Object W+P; [void][W]::ClientToScreen($h, [ref]$pt)
    $bmp = New-Object Drawing.Bitmap $r.Rt, $r.B
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object Drawing.Size $r.Rt, $r.B))
    $g.Dispose(); $bmp
}
function Shot([string]$name) { $b = Grab; $b.Save("$OutDir\$name.png", [Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }
# Dominant colour class of the screen centre: GREEN, BLACK or OTHER.
function Bg {
    # Sample the far left edge: no text, ring or bezel arc is ever drawn there.
    # Inside the round display, left of centre: no text or ring is drawn there.
    $b = Grab; $c = $b.GetPixel([int]($b.Width * 0.24), [int]($b.Height * 0.52)); $b.Dispose()
    if ($c.G -gt 150 -and $c.R -lt 60 -and $c.B -lt 60) { "GREEN" } elseif ($c.R + $c.G + $c.B -lt 110) { "BLACK" }   # glass sheen lifts black a little else { "OTHER" }
}
# SDL reads WM_KEYDOWN/WM_KEYUP; hold time is the press length.
function Tap([char]$k, [int]$holdMs = 60) {
    # SDL builds the key from the scancode in lParam bits 16-23, so send a real one.
    $vk = [uint32][char]::ToUpper($k)
    $sc = [W]::MapVirtualKey($vk, 0)
    $down = [IntPtr](1 -bor ($sc -shl 16))
    $up   = [IntPtr]([int64](1 -bor ($sc -shl 16) -bor 0xC0000000))
    [void][W]::PostMessage($h, 0x0100, [IntPtr][int]$vk, $down)
    Start-Sleep -Milliseconds $holdMs
    [void][W]::PostMessage($h, 0x0101, [IntPtr][int]$vk, $up)
}
# A fingerprint of the picture, so a step can prove the screen really changed.
function Sig {
    $b = Grab; $sum = 0L; $n = 0
    for ($y = 0; $y -lt $b.Height; $y += 6) { for ($x = 0; $x -lt $b.Width; $x += 6) {
        $c = $b.GetPixel($x, $y); $sum = ($sum * 31 + $c.R * 3 + $c.G * 5 + $c.B * 7) % 1000000007; $n++ } }
    $b.Dispose(); $sum
}
function HapticChecks([bool]$expectBuzzer) {
    "== haptics reached the kernel (log is flushed on exit)"
    $null = $p.WaitForExit(3000)
    $logText = [string](Get-Content "$OutDir\sim.log" -Raw)
    Check ([bool]($logText -match "Mock\.Vibro::play")) "service played vibro"
    Check ([bool]($logText -match "effect=4,")) "stimulus cue is the SHARP_CLICK effect (4)"
    Check ([bool]($logText -match "effect=10,")) "early press cue is the DOUBLE_CLICK effect (10)"
    if ($expectBuzzer) { Check ([bool]($logText -match "Mock\.Buzzer::play")) "service played the buzzer (lapse / done cues)" }
}
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 50 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 200; $p.Refresh(); $h = $p.MainWindowHandle }
    if ($h -eq [IntPtr]::Zero) { throw "no window" }
    Start-Sleep -Milliseconds 1500                       # let the first frame paint

    "== intro"
    Shot "01_intro"; $introSig = Sig
    Check ((Bg) -eq "BLACK") "intro background is black"

    "== start, wait"
    Tap '3'; Start-Sleep -Milliseconds 500
    Shot "02_wait"; $waitSig = Sig
    Check ($waitSig -ne $introSig) "R1 changes the screen (intro -> WAIT)"
    Check ((Bg) -eq "BLACK") "wait screen is black (no stimulus yet)"

    "== too early"
    Tap '1'; Start-Sleep -Milliseconds 250
    Shot "03_too_soon"; $earlySig = Sig
    Check ($earlySig -ne $waitSig) "early press shows TOO SOON (screen changes)"
    Check ((Bg) -eq "BLACK") "early press does not trigger GO"

    "== wait for GO"
    $t0 = Get-Date; $seen = $false
    while (((Get-Date) - $t0).TotalSeconds -lt 16 -and -not $seen) { Start-Sleep -Milliseconds 30; $seen = ((Bg) -eq "GREEN") }
    Check $seen "GO (green) appears within the delay window"
    if ($seen) {
        $goAt = Get-Date
        Start-Sleep -Milliseconds ([Math]::Max(0, $Reaction - 120))
        Shot "04_go"
        Tap '2' 40
        $elapsed = ((Get-Date) - $goAt).TotalMilliseconds
        Start-Sleep -Milliseconds 300
        Shot "05_feedback"
        Check ((Bg) -eq "BLACK") "feedback screen leaves the green GO"
        "  (driver reaction about $([int]$elapsed) ms)"
    }

    if ($Full) {
        "== full 3-minute session, answering every GO in about $Reaction ms"
        $t0 = Get-Date; $answered = 0; $prev = ""; $resultSeen = $false
        while (((Get-Date) - $t0).TotalSeconds -lt 260) {
            Start-Sleep -Milliseconds 25
            $bg = Bg
            if ($bg -eq "GREEN" -and $prev -ne "GREEN") {
                Start-Sleep -Milliseconds ([Math]::Max(0, $Reaction - 60))
                Tap '2' 30; $answered++
            }
            $prev = $bg
            if ($answered -ge 8 -and ((Get-Date) - $t0).TotalSeconds -gt 185) {
                # past 3 minutes the engine should be heading for DONE
                if (Select-String -Path "$OutDir\sim.log" -Pattern "RESULT" -Quiet) { break }
            }
        }
        Start-Sleep -Milliseconds 1500
        Shot "07_result"
        Tap '2'; Start-Sleep -Milliseconds 400; Shot "08_trend"
        Tap '2'; Start-Sleep -Milliseconds 400; Shot "09_details"
        Tap '2'; Start-Sleep -Milliseconds 400
        Check ([IO.File]::Exists("$fsRoot\history.bin")) "service saved the test to history.bin"
        "  answered $answered stimuli in $([int]((Get-Date) - $t0).TotalSeconds) s"
        Check ($answered -ge 15) "session produced a realistic number of trials"
        Check ($answered -le 80) "session did not overrun the trial buffer"
        Tap '4'; Start-Sleep -Milliseconds 800; $p.Refresh()
        Check $p.HasExited "R2 on the result screen exits the app"
        HapticChecks $true
        if ($script:fail.Count) { "FAILED: $($script:fail.Count)"; exit 1 } else { "ALL PASSED"; exit 0 }
    }

    "== cancel"
    Tap '4'; Start-Sleep -Milliseconds 400
    Shot "06_cancelled"; $cancelSig = Sig
    Check ($cancelSig -eq $introSig) "R2 mid-run returns to the intro, same picture"
    Check (-not $p.HasExited) "R2 mid-run cancels without closing the app"

    "== exit from intro"
    Tap '4'; Start-Sleep -Milliseconds 800
    $p.Refresh()
    Check $p.HasExited "R2 on intro exits the app"
    HapticChecks $false
}
catch { $script:fail += $_.Exception.Message; "ERROR $($_.Exception.Message)" }
finally { if (-not $p.HasExited) { $p.Kill() } }

if ($script:fail.Count) { "FAILED: $($script:fail.Count)"; exit 1 } else { "ALL PASSED"; exit 0 }





