# 🛑 Script para Detener Mosquitto MQTT

Write-Host "╔════════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║   DETENIENDO MOSQUITTO MQTT BROKER    ║" -ForegroundColor Yellow
Write-Host "╚════════════════════════════════════════╝" -ForegroundColor Yellow
Write-Host ""

# Verificar si Docker está corriendo
Write-Host "📦 Verificando Docker..." -ForegroundColor Yellow
try {
    docker info | Out-Null
    Write-Host "✅ Docker está corriendo" -ForegroundColor Green
}
catch {
    Write-Host "❌ Docker no está corriendo" -ForegroundColor Red
    Write-Host ""
    Read-Host "Presiona Enter para salir"
    exit 0
}

Write-Host ""

# Verificar si Mosquitto está corriendo
Write-Host "🔍 Verificando contenedor de Mosquitto..." -ForegroundColor Yellow
$container = docker ps --filter "name=mosquitto" --format "{{.Names}}"

if ($container) {
    Write-Host "🛑 Deteniendo Mosquitto..." -ForegroundColor Yellow
    docker stop mosquitto
    Write-Host "✅ Mosquitto detenido!" -ForegroundColor Green
} else {
    Write-Host "ℹ️  Mosquitto no está corriendo" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "✨ Proceso completado" -ForegroundColor Green
Write-Host ""

Start-Sleep -Seconds 2
