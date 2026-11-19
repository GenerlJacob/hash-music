<#
.SYNOPSIS
  Plays a .mid file through Windows' built-in MIDI sequencer (the one Windows Media Player uses),
  so the demo can play songs straight from the terminal.

.EXAMPLE
  .\tools\play.ps1 song.mid
  .\tools\play.ps1 jacob.mid, jacob_again.mid, jacob1.mid    # back to back
  .\tools\play.ps1 song.mid -CheckOnly                        # open it and report its length, no sound
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Path,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'

if (-not ('HashMusic.Mci' -as [type])) {
    Add-Type -Namespace HashMusic -Name Mci -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("winmm.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int mciSendString(string command, System.Text.StringBuilder result, int resultLength, System.IntPtr callback);
[System.Runtime.InteropServices.DllImport("winmm.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern bool mciGetErrorString(int error, System.Text.StringBuilder text, int textLength);
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int GetShortPathName(string longPath, System.Text.StringBuilder shortPath, int bufferLength);
'@
}

# The MCI sequencer rejects long paths ("cannot find the specified file"), so hand it the 8.3 short
# form of the path when Windows has one.
function Get-ShortPath([string]$LongPath) {
    $buffer = New-Object System.Text.StringBuilder 260
    $length = [HashMusic.Mci]::GetShortPathName($LongPath, $buffer, $buffer.Capacity)
    if ($length -gt 0 -and $length -lt $buffer.Capacity) { return $buffer.ToString() }
    return $LongPath
}

function Send-Mci([string]$Command) {
    $result = New-Object System.Text.StringBuilder 256
    $code = [HashMusic.Mci]::mciSendString($Command, $result, $result.Capacity, [IntPtr]::Zero)
    if ($code -ne 0) {
        $message = New-Object System.Text.StringBuilder 256
        [void][HashMusic.Mci]::mciGetErrorString($code, $message, $message.Capacity)
        throw "MIDI player error ${code}: $($message.ToString())"
    }
    $result.ToString()
}

foreach ($item in $Path) {
    $file = (Resolve-Path -LiteralPath $item).Path
    [void](Send-Mci "open `"$(Get-ShortPath $file)`" type sequencer alias hashsong")
    try {
        [void](Send-Mci 'set hashsong time format milliseconds')
        $seconds = [int](Send-Mci 'status hashsong length') / 1000  # up to the last note-off
        if ($CheckOnly) {
            '{0}: opened by the Windows MIDI sequencer ({1:N1} s of notes)' -f (Split-Path $file -Leaf), $seconds
        } else {
            'Playing {0} ({1:N1} s)...' -f (Split-Path $file -Leaf), $seconds
            [void](Send-Mci 'play hashsong wait')
        }
    } finally {
        [void](Send-Mci 'close hashsong')
    }
}
