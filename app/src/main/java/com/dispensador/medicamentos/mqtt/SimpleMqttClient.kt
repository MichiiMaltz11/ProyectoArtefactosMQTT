package com.dispensador.medicamentos.mqtt

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.eclipse.paho.client.mqttv3.*
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import java.util.concurrent.Executors

/**
 * Cliente MQTT personalizado sin dependencias de LocalBroadcastManager
 * Usa solo Paho Java Client (sin Android Service)
 */
class SimpleMqttClient(
    private val context: Context,
    private val brokerUrl: String,
    private val clientId: String
) {
    private var mqttClient: MqttAsyncClient? = null
    private val scope = CoroutineScope(Dispatchers.IO)
    private val executor = Executors.newSingleThreadExecutor()
    
    private val _connectionState = MutableStateFlow(false)
    val connectionState: StateFlow<Boolean> = _connectionState.asStateFlow()
    
    private val _messages = MutableStateFlow<Map<String, String>>(emptyMap())
    val messages: StateFlow<Map<String, String>> = _messages.asStateFlow()
    
    // Callback para procesar mensajes inmediatamente
    var onMessageCallback: ((String, String) -> Unit)? = null
    
    // Callback para notificar reconexión automática
    var onAutoReconnectCallback: (() -> Unit)? = null
    
    companion object {
        private const val TAG = "SimpleMqttClient"
        private const val CONNECTION_TIMEOUT = 10
        private const val KEEP_ALIVE_INTERVAL = 20
    }
    
    private fun iniciarMonitoreoEstado() {
        scope.launch {
            while (true) {
                kotlinx.coroutines.delay(2000) // Verificar cada 2 segundos
                
                val client = mqttClient
                if (client != null) {
                    val estaConectado = client.isConnected
                    
                    // Si el estado cambió a conectado y el StateFlow decía desconectado
                    if (estaConectado && !_connectionState.value) {
                        Log.d(TAG, "🔄 Reconexión automática detectada")
                        _connectionState.value = true
                        
                        // Re-suscribirse a los topics
                        subscribeToTopics()
                        
                        // Notificar callback
                        onAutoReconnectCallback?.invoke()
                    }
                    // Si el estado cambió a desconectado
                    else if (!estaConectado && _connectionState.value) {
                        Log.w(TAG, "⚠️ Desconexión detectada en monitoreo")
                        _connectionState.value = false
                    }
                }
            }
        }
    }
    
    init {
        try {
            val persistence = MemoryPersistence()
            mqttClient = MqttAsyncClient(brokerUrl, clientId, persistence)
            
            mqttClient?.setCallback(object : MqttCallback {
                override fun connectionLost(cause: Throwable?) {
                    Log.e(TAG, "❌ Conexión perdida", cause)
                    _connectionState.value = false
                    
                    // Log adicional para debug
                    val reason = cause?.message ?: "Desconocida"
                    Log.w(TAG, "⚠️ Razón de desconexión: $reason")
                    Log.d(TAG, "🔄 AutoReconnect está habilitado, el cliente intentará reconectar...")
                }
                
                override fun messageArrived(topic: String?, message: MqttMessage?) {
                    scope.launch(Dispatchers.Main) {
                        Log.d(TAG, "📨 messageArrived llamado - topic: $topic, message null?: ${message == null}")
                        if (topic != null && message != null) {
                            val payload = String(message.payload)
                            val size = payload.length
                            Log.d(TAG, "📩 Mensaje recibido en topic: $topic")
                            Log.d(TAG, "📏 Tamaño del payload: $size bytes")
                            if (size < 200) {
                                Log.d(TAG, "📦 Payload: $payload")
                            } else {
                                Log.d(TAG, "📦 Payload (primeros 200 chars): ${payload.take(200)}...")
                            }
                            _messages.value = _messages.value + (topic to payload)
                            // Llamar callback inmediatamente
                            onMessageCallback?.invoke(topic, payload)
                            Log.d(TAG, "✅ Callback invocado para topic: $topic")
                        } else {
                            Log.w(TAG, "⚠️ Mensaje recibido con topic o message null")
                        }
                    }
                }
                
                override fun deliveryComplete(token: IMqttDeliveryToken?) {
                    Log.d(TAG, "✅ Mensaje entregado")
                }
            })
            
            Log.d(TAG, "Cliente MQTT inicializado")
            
            // Iniciar monitoreo de estado de conexión
            iniciarMonitoreoEstado()
        } catch (e: Exception) {
            Log.e(TAG, "Error inicializando cliente MQTT", e)
        }
    }
    
    fun connect(onSuccess: () -> Unit = {}, onFailure: (Throwable) -> Unit = {}) {
        executor.execute {
            try {
                val client = mqttClient
                if (client == null) {
                    val error = Exception("Cliente MQTT no inicializado")
                    Log.e(TAG, "❌ Error: cliente null")
                    scope.launch(Dispatchers.Main) { onFailure(error) }
                    return@execute
                }
                
                if (client.isConnected) {
                    Log.d(TAG, "✅ Ya conectado")
                    _connectionState.value = true
                    scope.launch(Dispatchers.Main) { onSuccess() }
                    return@execute
                }
                
                val options = MqttConnectOptions().apply {
                    isCleanSession = true
                    connectionTimeout = CONNECTION_TIMEOUT
                    keepAliveInterval = KEEP_ALIVE_INTERVAL
                    isAutomaticReconnect = true
                    maxInflight = 100 // Permitir más mensajes en vuelo
                    // Nota: maxMessageSize solo existe en MqttAsyncClient v5, pero v3 usa 2048 por defecto
                }
                
                Log.d(TAG, "🔄 Conectando a $brokerUrl...")
                
                client.connect(options, null, object : IMqttActionListener {
                    override fun onSuccess(asyncActionToken: IMqttToken?) {
                        Log.d(TAG, "✅ Conectado exitosamente")
                        _connectionState.value = true
                        
                        // Suscribirse a topics
                        subscribeToTopics()
                        
                        scope.launch(Dispatchers.Main) { onSuccess() }
                    }
                    
                    override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                        Log.e(TAG, "❌ Error al conectar", exception)
                        _connectionState.value = false
                        scope.launch(Dispatchers.Main) {
                            onFailure(exception ?: Exception("Error desconocido"))
                        }
                    }
                })
            } catch (e: Exception) {
                Log.e(TAG, "❌ Excepción al conectar", e)
                _connectionState.value = false
                scope.launch(Dispatchers.Main) { onFailure(e) }
            }
        }
    }
    
    private fun subscribeToTopics() {
        val topics = arrayOf(
            MqttConfig.Topics.STATUS,
            MqttConfig.Topics.DOSIS_CONFIRMADA,
            MqttConfig.Topics.DOSIS_OMITIDA,
            MqttConfig.Topics.ALARMA_ACTIVA,
            MqttConfig.Topics.HISTORIAL_DATOS,
            MqttConfig.Topics.DOSIS_ACTUALES
        )
        
        topics.forEach { topic ->
            try {
                mqttClient?.subscribe(topic, 1, null, object : IMqttActionListener {
                    override fun onSuccess(asyncActionToken: IMqttToken?) {
                        Log.d(TAG, "✅ Suscrito a: $topic")
                    }
                    
                    override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                        Log.e(TAG, "❌ Error suscribiéndose a $topic", exception)
                    }
                })
            } catch (e: Exception) {
                Log.e(TAG, "❌ Excepción al suscribirse a $topic", e)
            }
        }
    }
    
    fun publish(topic: String, message: String, qos: Int = 1) {
        executor.execute {
            try {
                val client = mqttClient
                if (client == null || !client.isConnected) {
                    Log.e(TAG, "❌ No conectado, no se puede publicar")
                    return@execute
                }
                
                val mqttMessage = MqttMessage(message.toByteArray()).apply {
                    this.qos = qos
                    isRetained = false
                }
                
                client.publish(topic, mqttMessage, null, object : IMqttActionListener {
                    override fun onSuccess(asyncActionToken: IMqttToken?) {
                        Log.d(TAG, "✅ Publicado: $topic = $message")
                    }
                    
                    override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                        Log.e(TAG, "❌ Error publicando en $topic", exception)
                    }
                })
            } catch (e: Exception) {
                Log.e(TAG, "❌ Excepción al publicar", e)
            }
        }
    }
    
    fun disconnect() {
        executor.execute {
            try {
                mqttClient?.disconnect(null, object : IMqttActionListener {
                    override fun onSuccess(asyncActionToken: IMqttToken?) {
                        Log.d(TAG, "🔌 Desconectado")
                        _connectionState.value = false
                    }
                    
                    override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                        Log.e(TAG, "❌ Error al desconectar", exception)
                        _connectionState.value = false
                    }
                })
            } catch (e: Exception) {
                Log.e(TAG, "❌ Excepción al desconectar", e)
                _connectionState.value = false
            }
        }
    }
    
    fun cleanup() {
        try {
            disconnect()
            mqttClient?.close()
            executor.shutdown()
            Log.d(TAG, "🧹 Cliente limpiado")
        } catch (e: Exception) {
            Log.e(TAG, "Error al limpiar cliente", e)
        }
    }
}
