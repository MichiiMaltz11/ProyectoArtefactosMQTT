# Script para Iniciar Mosquitto MQTT

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "   INICIANDO MOSQUITTO MQTT BROKER     " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Verificar si Docker esta corriendo
Write-Host "[*] Verificando Docker Desktop..." -ForegroundColor Yellow
$dockerRunning = $false
$maxAttempts = 30
$attempt = 0

while (-not $dockerRunning -and $attempt -lt $maxAttempts) {
    try {
        docker info | Out-Null
        $dockerRunning = $true
        Write-Host "[OK] Docker Desktop esta corriendo" -ForegroundColor Green
    }
    catch {
        $attempt++
        if ($attempt -eq 1) {
            Write-Host "[*] Docker Desktop no esta corriendo, iniciando..." -ForegroundColor Yellow
            Start-Process "C:\Program Files\Docker\Docker\Docker Desktop.exe" -ErrorAction SilentlyContinue
        }
        Write-Host "   Esperando Docker... ($attempt/$maxAttempts)" -ForegroundColor Gray
        Start-Sleep -Seconds 2
    }
}

if (-not $dockerRunning) {
    Write-Host "[ERROR] Docker Desktop no pudo iniciar" -ForegroundColor Red
    Write-Host "   Por favor, inicia Docker Desktop manualmente" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Presiona Enter para salir"
    exit 1
}

Write-Host ""

# Cambiar a directorio de Mosquitto
Write-Host "[*] Cambiando a directorio de Mosquitto..." -ForegroundColor Yellow
Set-Location "C:\Users\alima\mosquitto-mqtt"

# Verificar si Mosquitto ya esta corriendo
Write-Host "[*] Verificando contenedor de Mosquitto..." -ForegroundColor Yellow
$container = docker ps -a --filter "name=mosquitto" --format "{{.Status}}"

if ($container -like "*Up*") {
    Write-Host "[OK] Mosquitto ya esta corriendo!" -ForegroundColor Green
} elseif ($container) {
    Write-Host "[*] Iniciando contenedor existente..." -ForegroundColor Yellow
    docker start mosquitto
    Write-Host "[OK] Mosquitto iniciado!" -ForegroundColor Green
} else {
    Write-Host "[*] Creando e iniciando contenedor nuevo..." -ForegroundColor Yellow
    docker-compose up -d
    Write-Host "[OK] Mosquitto creado e iniciado!" -ForegroundColor Green
}

Write-Host ""

# Obtener IP
Write-Host "[*] Tu configuracion de red:" -ForegroundColor Cyan
$ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.InterfaceAlias -like "*Wi-Fi*" -or $_.InterfaceAlias -like "*Ethernet*"} | Select-Object -First 1).IPAddress
Write-Host "   IP de tu PC: $ip" -ForegroundColor White
Write-Host ""

# Verificar puerto
Write-Host "[*] Verificando puerto 1883..." -ForegroundColor Yellow
Start-Sleep -Seconds 2
$port = netstat -ano | findstr :1883
if ($port) {
    Write-Host "[OK] Puerto 1883 esta abierto y escuchando" -ForegroundColor Green
} else {
    Write-Host "[AVISO] Puerto 1883 no detectado, esperando..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "   MOSQUITTO ESTA LISTO!                " -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Broker MQTT: tcp://$($ip):1883" -ForegroundColor Cyan
Write-Host ""
Write-Host "Comandos utiles:" -ForegroundColor Yellow
Write-Host "   Ver logs:     docker logs mosquitto -f" -ForegroundColor Gray
Write-Host "   Detener:      docker stop mosquitto" -ForegroundColor Gray
Write-Host "   Reiniciar:    docker restart mosquitto" -ForegroundColor Gray
Write-Host ""

# Preguntar si desea ver logs
$response = Read-Host "Deseas ver los logs en tiempo real? (s/n)"
if ($response -eq "s" -or $response -eq "S") {
    Write-Host ""
    Write-Host "Mostrando logs (Ctrl+C para salir)..." -ForegroundColor Cyan
    Write-Host ""
    docker logs mosquitto -f
} else {
    Write-Host ""
    Write-Host "Listo! Puedes cerrar esta ventana" -ForegroundColor Green
    Start-Sleep -Seconds 2
}
