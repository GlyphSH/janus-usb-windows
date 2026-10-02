' Silent launcher for janus.ps1. Runs PowerShell with no console window,
' captures stdout/stderr, and shows the result in a message box.
' Intended for double-click launches, shortcuts, or scheduled tasks.
Option Explicit

Dim shell, fso, scriptDir, psScript, cmd, arg, exec, outText, errText, code
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
psScript = fso.BuildPath(scriptDir, "janus.ps1")

cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File """ & psScript & """"
' cmd.exe quoting: a literal " inside a double-quoted argument is escaped
' as "" so a path like C:\with "quotes" survives the handoff to powershell.
For Each arg In WScript.Arguments
    cmd = cmd & " """ & Replace(arg, """", """""") & """"
Next

Set exec = shell.Exec(cmd)
Do While exec.Status = 0
    WScript.Sleep 100
Loop

outText = exec.StdOut.ReadAll()
errText = exec.StdErr.ReadAll()
code = exec.ExitCode

If code = 0 Then
    WScript.Echo outText
Else
    WScript.Echo "Transfer failed (exit " & code & "):" & vbCrLf & errText
End If
