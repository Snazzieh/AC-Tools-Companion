$ErrorActionPreference = "Stop"

$piHost = if ($args.Count -ge 1) { $args[0] } else { "172.20.10.2" }
$piTarget = "s@$piHost`:~/accesspi/"
$ssh = "C:\Windows\System32\OpenSSH\ssh.exe"
$scp = "C:\Windows\System32\OpenSSH\scp.exe"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Creating ~/accesspi on $piHost..."
& $ssh "s@$piHost" "mkdir -p ~/accesspi"

Write-Host "Copying AccessPi files..."
& $scp `
    "$here\accesspi.py" `
    "$here\requirements.txt" `
    "$here\README.md" `
    "$here\run-kiosk.sh" `
    "$here\run-direct.sh" `
    "$here\run-webview.sh" `
    "$here\run-webview-proxy.sh" `
    "$here\access_webview.py" `
    "$here\install-commands.sh" `
    $piTarget

Write-Host "Installing short commands..."
& $ssh "s@$piHost" "cd ~/accesspi && chmod +x run-kiosk.sh run-direct.sh run-webview.sh run-webview-proxy.sh access_webview.py install-commands.sh && ./install-commands.sh"

Write-Host "Done. On the Pi you can now run: access, accesslite, accessdirect, accessdirectlite, accesswebview, accesswebviewproxy, kiosk, kiosklite, proxy, scanaccess, iphone, statusaccess, stopaccess"
