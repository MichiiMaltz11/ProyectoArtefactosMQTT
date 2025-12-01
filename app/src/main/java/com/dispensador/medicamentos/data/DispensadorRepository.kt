package com.dispensador.medicamentos.data

import android.content.Context
import android.util.Log
import com.dispensador.medicamentos.mqtt.MqttClientManager
import com.dispensador.medicamentos.mqtt.MqttConfig
import com.dispensador.medicamentos.notifications.NotificationHelper
import com.google.gson.Gson
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.*

class DispensadorRepository(context: Context) {
    private val database = DispensadorDatabase.getDatabase(context)
    private val dosisDao = database.dosisDao()
    private val historialDao = database.historialDosisDao()
    
    private val mqttClient = MqttClientManager(context)
    private val gson = Gson()
    private val notificationHelper = NotificationHelper(context)
    
    // Estado de conexión MQTT
    val connectionState: StateFlow<Boolean> = mqttClient.connectionState
    val mqttMessages: StateFlow<Map<String, String>> = mqttClient.messages
    
    init {
        // Configurar callback para procesar mensajes inmediatamente
        mqttClient.setMessageCallback { topic, payload ->
            CoroutineScope(Dispatchers.IO).launch {
                procesarMensajeMqtt(topic, payload)
            }
        }
        
        // Configurar callback para reconexión automática
        mqttClient.setAutoReconnectCallback {
            Log.d("DispensadorRepository", "🔄 Reconexión automática exitosa, reenviando dosis...")
            CoroutineScope(Dispatchers.IO).launch {
                kotlinx.coroutines.delay(2000)
                enviarTodasLasDosisAlESP8266()
            }
        }
    }
    
    private suspend fun procesarMensajeMqtt(topic: String, payload: String) {
        when (topic) {
                "/dosis/confirmada" -> {
                    try {
                        val json = org.json.JSONObject(payload)
                        val dosisId = json.optInt("dosis_id", -1)
                        val medicamento = json.optString("medicamento", "Desconocido")
                        val compartimiento = json.optInt("compartimiento", 1)
                        val timestamp = json.optLong("timestamp", System.currentTimeMillis() / 1000) * 1000L
                        
                        // Agregar al historial con timestamp del ESP32
                        val historial = HistorialDosis(
                            dosisId = dosisId,
                            medicamento = medicamento,
                            compartimiento = compartimiento,
                            estado = EstadoDosis.TOMADA,
                            timestamp = timestamp
                        )
                        insertHistorial(historial)
                        
                        // Eliminar dosis por compartimiento (el ID del ESP32 no coincide con Room DB)
                        val dosis = dosisDao.getDosisByCompartimiento(compartimiento)
                        if (dosis != null) {
                            Log.d("DispensadorRepository", "📌 Dosis encontrada: ID=${dosis.id}, C$compartimiento, ${dosis.medicamento}")
                            deleteDosis(dosis)
                            Log.d("DispensadorRepository", "🗑️ Dosis eliminada del Dashboard: C$compartimiento")
                        } else {
                            Log.w("DispensadorRepository", "⚠️ No se encontró dosis en compartimiento $compartimiento")
                        }
                        
                        Log.d("DispensadorRepository", "✅ Dosis confirmada procesada y eliminada")
                    } catch (e: Exception) {
                        Log.e("DispensadorRepository", "Error procesando confirmación", e)
                    }
                }
                "/dosis/omitida" -> {
                    try {
                        val json = org.json.JSONObject(payload)
                        val dosisId = json.optInt("dosis_id", -1)
                        val medicamento = json.optString("medicamento", "Desconocido")
                        val compartimiento = json.optInt("compartimiento", 1)
                        val timestamp = json.optLong("timestamp", System.currentTimeMillis() / 1000) * 1000L
                        
                        // Agregar al historial como omitida con timestamp del ESP32
                        val historial = HistorialDosis(
                            dosisId = dosisId,
                            medicamento = medicamento,
                            compartimiento = compartimiento,
                            estado = EstadoDosis.OMITIDA,
                            timestamp = timestamp
                        )
                        insertHistorial(historial)
                        
                        // Eliminar dosis por compartimiento
                        val dosis = dosisDao.getDosisByCompartimiento(compartimiento)
                        if (dosis != null) {
                            Log.d("DispensadorRepository", "📌 Dosis encontrada: ID=${dosis.id}, C$compartimiento, ${dosis.medicamento}")
                            deleteDosis(dosis)
                            Log.d("DispensadorRepository", "🗑️ Dosis eliminada del Dashboard: C$compartimiento (omitida)")
                        } else {
                            Log.w("DispensadorRepository", "⚠️ No se encontró dosis en compartimiento $compartimiento (omitida)")
                        }
                        
                        Log.d("DispensadorRepository", "⚠️ Dosis omitida procesada")
                    } catch (e: Exception) {
                        Log.e("DispensadorRepository", "Error procesando omisión", e)
                    }
                }
                "/historial/datos" -> {
                    // ⚠️ ESP8266 NO PERSISTE HISTORIAL - La app Room DB es la única fuente de verdad
                    // Este topic ya no se usa porque el historial solo existe en la app
                    Log.d("DispensadorRepository", "ℹ️ Topic /historial/datos ignorado (ESP8266 no tiene historial persistente)")
                }
                "/alarma/activa" -> {
                    try {
                        val json = org.json.JSONObject(payload)
                        val medicamento = json.optString("medicamento", "Medicamento")
                        val compartimiento = json.optInt("compartimiento", 1)
                        val hora = json.optString("hora", "--:--")
                        
                        Log.d("DispensadorRepository", "⏰ Alarma activa: $medicamento - C$compartimiento")
                        
                        // Mostrar notificación
                        notificationHelper.mostrarNotificacionAlarma(medicamento, compartimiento, hora)
                    } catch (e: Exception) {
                        Log.e("DispensadorRepository", "Error procesando alarma activa", e)
                    }
                }
                "/dispensador/dosis/actuales" -> {
                    // ℹ️ ESP8266 envía sus dosis activas después de reiniciar
                    // La app verifica y elimina cualquier dosis que el ESP8266 ya procesó
                    try {
                        val json = org.json.JSONObject(payload)
                        val dosisArray = json.getJSONArray("dosis")
                        val totalEsp8266 = json.getInt("total")
                        
                        Log.d("DispensadorRepository", "📥 ESP8266 reporta $totalEsp8266 dosis activas")
                        
                        // Crear set de compartimientos que el ESP8266 tiene
                        val compartimentosEsp8266 = mutableSetOf<Int>()
                        for (i in 0 until dosisArray.length()) {
                            val dosisJson = dosisArray.getJSONObject(i)
                            val compartimiento = dosisJson.getInt("compartimiento")
                            compartimentosEsp8266.add(compartimiento)
                        }
                        
                        // Verificar en coroutine
                        CoroutineScope(Dispatchers.IO).launch {
                            val dosisApp = dosisDao.getAllDosisList()
                            
                            // Eliminar de la app cualquier dosis que NO esté en ESP8266
                            // (significa que el ESP8266 ya la procesó durante un reinicio)
                            for (dosis in dosisApp) {
                                if (dosis.compartimiento !in compartimentosEsp8266) {
                                    Log.d("DispensadorRepository", "🗑️ Limpiando C${dosis.compartimiento} (ya procesada por ESP8266)")
                                    dosisDao.deleteDosis(dosis)
                                }
                            }
                            
                            Log.d("DispensadorRepository", "✅ Limpieza de dosis completada")
                        }
                    } catch (e: Exception) {
                        Log.e("DispensadorRepository", "Error procesando dosis actuales", e)
                    }
                }
        }
    }
    
    // ==================== MQTT Operations ====================
    
    fun connectToMqtt(onSuccess: () -> Unit = {}, onFailure: (Throwable) -> Unit = {}) {
        try {
            android.util.Log.d("DispensadorRepository", "Intentando conectar a MQTT...")
            mqttClient.connect(
                onSuccess = {
                    android.util.Log.d("DispensadorRepository", "✅ Conexión MQTT exitosa")
                    // 🆕 NUEVA ARQUITECTURA: La app ENVÍA todas las dosis al ESP8266
                    CoroutineScope(Dispatchers.IO).launch {
                        android.util.Log.d("DispensadorRepository", "⏳ Esperando 2 segundos para que las suscripciones se completen...")
                        kotlinx.coroutines.delay(2000)
                        
                        android.util.Log.d("DispensadorRepository", "📤 Enviando todas las dosis al ESP8266...")
                        enviarTodasLasDosisAlESP8266()
                        
                        android.util.Log.d("DispensadorRepository", "✅ Sincronización inicial completada")
                    }
                    onSuccess()
                },
                onFailure = { error ->
                    android.util.Log.e("DispensadorRepository", "❌ Error al conectar MQTT", error)
                    onFailure(error)
                }
            )
        } catch (e: Exception) {
            android.util.Log.e("DispensadorRepository", "❌ Excepción al conectar", e)
            onFailure(e)
        }
    }
    
    fun disconnectFromMqtt() {
        try {
            mqttClient.disconnect()
            android.util.Log.d("DispensadorRepository", "🔌 Desconectado de MQTT")
        } catch (e: Exception) {
            android.util.Log.e("DispensadorRepository", "Error al desconectar", e)
        }
    }
    
    fun enviarConfiguracionDosis(dosis: Dosis) {
        val message = ConfigDosisMessage(
            dosis_id = dosis.id,
            hora = dosis.hora,
            compartimiento = dosis.compartimiento,
            medicamento = dosis.medicamento,
            activo = dosis.activo
        )
        val json = gson.toJson(message)
        mqttClient.publish(MqttConfig.Topics.CONFIG_DOSIS, json)
    }
    
    fun sincronizarHora() {
        val sdf = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.getDefault())
        val currentTime = sdf.format(Date())
        mqttClient.publish(MqttConfig.Topics.CONFIG_HORA, currentTime)
    }
    
    fun detenerAlarma() {
        mqttClient.publish(MqttConfig.Topics.DETENER_ALARMA, "stop")
    }
    
    fun limpiarTodasLasDosisESP32() {
        mqttClient.publish("/dispensador/limpiar", "clear")
        Log.d("DispensadorRepository", "🗑️ Comando limpiar todas las dosis enviado al ESP8266")
    }
    
    fun testLed(encender: Boolean) {
        mqttClient.publish(MqttConfig.Topics.TEST_LED, if (encender) "on" else "off")
    }
    
    fun testBuzzer(encender: Boolean) {
        mqttClient.publish(MqttConfig.Topics.TEST_BUZZER, if (encender) "on" else "off")
    }
    
    // ⚠️ FUNCIÓN ELIMINADA: solicitarHistorialESP32()
    // El ESP8266 NO guarda historial en memoria flash (solo 512 bytes EEPROM)
    // La app Room DB es la ÚNICA fuente de verdad para el historial
    
    fun solicitarDosisActuales() {
        Log.d("DispensadorRepository", "📤 Publicando solicitud de dosis al topic: ${MqttConfig.Topics.SOLICITAR_DOSIS}")
        mqttClient.publish(MqttConfig.Topics.SOLICITAR_DOSIS, "sync")
        Log.d("DispensadorRepository", "✅ Solicitud de dosis actuales enviada al ESP8266")
    }
    
    private suspend fun enviarTodasLasDosisAlESP8266() {
        try {
            // Primero limpiar todas las dosis del ESP8266
            limpiarTodasLasDosisESP32()
            kotlinx.coroutines.delay(500)
            
            // Obtener todas las dosis de la app
            val todasLasDosis = dosisDao.getAllDosisList()
            Log.d("DispensadorRepository", "📋 Enviando ${todasLasDosis.size} dosis al ESP8266...")
            
            // Enviar cada dosis una por una
            todasLasDosis.forEach { dosis ->
                enviarConfiguracionDosis(dosis)
                Log.d("DispensadorRepository", "📤 Dosis enviada: C${dosis.compartimiento} - ${dosis.medicamento} a las ${dosis.hora}")
                kotlinx.coroutines.delay(300) // Pequeño delay para no saturar
            }
            
            Log.d("DispensadorRepository", "✅ Todas las dosis sincronizadas con ESP8266")
        } catch (e: Exception) {
            Log.e("DispensadorRepository", "❌ Error enviando dosis al ESP8266", e)
        }
    }
    
    // ==================== Database Operations - Dosis ====================
    
    fun getAllDosis(): Flow<List<Dosis>> = dosisDao.getAllDosis()
    
    suspend fun getDosisById(id: Int): Dosis? = dosisDao.getDosisById(id)
    
    suspend fun insertDosis(dosis: Dosis): Long {
        // Verificar si ya existe una dosis en ese compartimento
        val dosisExistente = dosisDao.getDosisByCompartimiento(dosis.compartimiento)
        if (dosisExistente != null) {
            Log.w("DispensadorRepository", "⚠️ Ya existe una dosis en el compartimento ${dosis.compartimiento}")
            throw IllegalStateException("El compartimento ${dosis.compartimiento} ya tiene una dosis asignada. Elimínala primero.")
        }
        
        val id = dosisDao.insertDosis(dosis)
        // Enviar configuración al ESP32
        val dosisWithId = dosis.copy(id = id.toInt())
        enviarConfiguracionDosis(dosisWithId)
        return id
    }
    
    suspend fun updateDosis(dosis: Dosis) {
        dosisDao.updateDosis(dosis)
        // Actualizar configuración en ESP32
        enviarConfiguracionDosis(dosis)
    }
    
    suspend fun deleteDosis(dosis: Dosis) {
        dosisDao.deleteDosis(dosis)
        // Enviar comando de eliminación al ESP32 usando compartimiento
        enviarEliminacionDosis(dosis.compartimiento)
    }
    
    private fun enviarEliminacionDosis(compartimiento: Int) {
        val json = """{"compartimiento":$compartimiento}"""
        mqttClient.publish(MqttConfig.Topics.ELIMINAR_DOSIS, json)
        Log.d("DispensadorRepository", "🗑️ Comando eliminar enviado para compartimiento C$compartimiento")
    }
    
    // ==================== Database Operations - Historial ====================
    
    companion object {
        private const val MAX_HISTORIAL = 100 // Mismo límite que ESP32
    }
    
    fun getAllHistorial(): Flow<List<HistorialDosis>> = historialDao.getAllHistorial()
    
    fun getHistorialDesde(startTime: Long): Flow<List<HistorialDosis>> =
        historialDao.getHistorialDesde(startTime)
    
    suspend fun insertHistorial(historial: HistorialDosis) {
        // Insertar nuevo registro
        historialDao.insertHistorial(historial)
        
        // Verificar si superamos el límite de 100 registros (FIFO)
        val totalRegistros = historialDao.getHistorialCount()
        if (totalRegistros > MAX_HISTORIAL) {
            // Eliminar los registros más antiguos para mantener solo 100
            val registrosAEliminar = totalRegistros - MAX_HISTORIAL
            repeat(registrosAEliminar) {
                val oldest = historialDao.getOldestHistorial()
                oldest?.let {
                    historialDao.deleteHistorial(it)
                    Log.d("DispensadorRepository", "🗑️ FIFO: Eliminado registro antiguo (timestamp: ${it.timestamp})")
                }
            }
            Log.d("DispensadorRepository", "♻️ Historial limitado a $MAX_HISTORIAL registros")
        }
    }
    
    suspend fun clearHistorial() {
        historialDao.clearHistorial()
    }
}
