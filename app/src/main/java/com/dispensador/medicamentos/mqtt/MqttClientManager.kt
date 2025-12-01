package com.dispensador.medicamentos.mqtt

import android.content.Context
import kotlinx.coroutines.flow.StateFlow

/**
 * Wrapper para SimpleMqttClient sin dependencias de LocalBroadcastManager
 */
class MqttClientManager(context: Context) {
    private val mqttClient: SimpleMqttClient = SimpleMqttClient(
        context,
        MqttConfig.BROKER_URL,
        MqttConfig.CLIENT_ID
    )
    
    val connectionState: StateFlow<Boolean> = mqttClient.connectionState
    val messages: StateFlow<Map<String, String>> = mqttClient.messages
    
    fun setMessageCallback(callback: (String, String) -> Unit) {
        mqttClient.onMessageCallback = callback
    }
    
    fun setAutoReconnectCallback(callback: () -> Unit) {
        mqttClient.onAutoReconnectCallback = callback
    }
    
    fun connect(onSuccess: () -> Unit = {}, onFailure: (Throwable) -> Unit = {}) {
        mqttClient.connect(onSuccess, onFailure)
    }
    
    fun publish(topic: String, message: String, qos: Int = 1) {
        mqttClient.publish(topic, message, qos)
    }
    
    fun disconnect() {
        mqttClient.disconnect()
    }
    
    fun cleanup() {
        mqttClient.cleanup()
    }
}
