package com.dispensador.medicamentos.ui.viewmodel

import android.app.Application
import android.widget.Toast
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.dispensador.medicamentos.data.DispensadorRepository
import com.dispensador.medicamentos.data.Dosis
import com.dispensador.medicamentos.data.HistorialDosis
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class DispensadorViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DispensadorRepository(application)
    private val context = application.applicationContext
    
    // Estado de conexión MQTT
    val connectionState: StateFlow<Boolean> = repository.connectionState
    
    // Lista de dosis (StateFlow mutable internamente, inmutable externamente)
    private val _todasLasDosis = MutableStateFlow<List<Dosis>>(emptyList())
    val todasLasDosis: StateFlow<List<Dosis>> = _todasLasDosis
    
    // Historial (StateFlow mutable internamente, inmutable externamente)
    private val _historial = MutableStateFlow<List<HistorialDosis>>(emptyList())
    val historial: StateFlow<List<HistorialDosis>> = _historial
    
    init {
        viewModelScope.launch {
            repository.getAllDosis().collect { dosis ->
                _todasLasDosis.value = dosis
            }
        }
        
        viewModelScope.launch {
            repository.getAllHistorial().collect { hist ->
                _historial.value = hist
            }
        }
        
        // Conexión automática al iniciar la app
        conectarAutomaticamente()
        
        // Monitoreo de reconexión automática
        iniciarMonitoreoReconexion()
    }
    
    private fun conectarAutomaticamente() {
        viewModelScope.launch {
            // Esperar 1 segundo para que la UI se inicialice
            kotlinx.coroutines.delay(1000)
            
            android.util.Log.d("DispensadorViewModel", "🔄 Iniciando conexión automática...")
            
            repository.connectToMqtt(
                onSuccess = {
                    android.util.Log.d("DispensadorViewModel", "✅ Conexión automática exitosa")
                    // No mostrar toast para no molestar al usuario
                },
                onFailure = { error ->
                    android.util.Log.e("DispensadorViewModel", "❌ Error en conexión automática: ${error.message}")
                    // Reintentar en 5 segundos
                    viewModelScope.launch {
                        kotlinx.coroutines.delay(5000)
                        reintentarConexion()
                    }
                }
            )
        }
    }
    
    private fun reintentarConexion() {
        if (!connectionState.value) {
            android.util.Log.d("DispensadorViewModel", "🔄 Reintentando conexión...")
            
            repository.connectToMqtt(
                onSuccess = {
                    android.util.Log.d("DispensadorViewModel", "✅ Reconexión exitosa")
                },
                onFailure = { error ->
                    android.util.Log.e("DispensadorViewModel", "❌ Reintento fallido: ${error.message}")
                    // Reintentar en 10 segundos
                    viewModelScope.launch {
                        kotlinx.coroutines.delay(10000)
                        reintentarConexion()
                    }
                }
            )
        }
    }
    
    private fun iniciarMonitoreoReconexion() {
        viewModelScope.launch {
            var estadoAnterior = connectionState.value
            
            connectionState.collect { conectado ->
                // Solo actuar cuando cambia de conectado a desconectado
                if (estadoAnterior && !conectado) {
                    android.util.Log.w("DispensadorViewModel", "⚠️ Conexión perdida, intentando reconectar...")
                    // Esperar 3 segundos antes de reintentar
                    kotlinx.coroutines.delay(3000)
                    reintentarConexion()
                } else if (!estadoAnterior && conectado) {
                    android.util.Log.d("DispensadorViewModel", "✅ Conexión restablecida")
                }
                
                // Actualizar estado anterior
                estadoAnterior = conectado
            }
        }
    }
    
    // ==================== MQTT Operations ====================
    
    // Conexión manual desde Ajustes (con feedback visual)
    fun conectar() {
        viewModelScope.launch {
            try {
                android.util.Log.d("DispensadorViewModel", "🔄 Conexión manual desde Ajustes...")
                Toast.makeText(context, "Conectando a MQTT...", Toast.LENGTH_SHORT).show()
                
                repository.connectToMqtt(
                    onSuccess = {
                        android.util.Log.d("DispensadorViewModel", "✅ Conexión manual exitosa")
                        Toast.makeText(context, "✅ Conectado exitosamente", Toast.LENGTH_SHORT).show()
                        // Sincronizar hora al conectar
                        repository.sincronizarHora()
                    },
                    onFailure = { error ->
                        android.util.Log.e("DispensadorViewModel", "❌ Error conectando MQTT", error)
                        val errorMsg = error.message ?: "Error desconocido"
                        Toast.makeText(context, "❌ Error: $errorMsg", Toast.LENGTH_LONG).show()
                    }
                )
            } catch (e: Exception) {
                android.util.Log.e("DispensadorViewModel", "❌ Excepción al conectar", e)
                Toast.makeText(context, "❌ Excepción: ${e.message}", Toast.LENGTH_LONG).show()
            }
        }
    }
    
    fun desconectar() {
        viewModelScope.launch {
            try {
                android.util.Log.d("DispensadorViewModel", "🔌 Desconexión manual desde Ajustes")
                Toast.makeText(context, "Desconectando...", Toast.LENGTH_SHORT).show()
                repository.disconnectFromMqtt()
                Toast.makeText(context, "✅ Desconectado", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                android.util.Log.e("DispensadorViewModel", "Error al desconectar", e)
                Toast.makeText(context, "❌ Error al desconectar", Toast.LENGTH_SHORT).show()
            }
        }
    }
    
    fun sincronizarHora() {
        repository.sincronizarHora()
    }
    
    fun detenerAlarma() {
        repository.detenerAlarma()
    }
    
    fun testLed(encender: Boolean) {
        repository.testLed(encender)
    }
    
    fun testBuzzer(encender: Boolean) {
        repository.testBuzzer(encender)
    }
    
    // ==================== Database Operations ====================
    
    fun agregarDosis(dosis: Dosis) {
        viewModelScope.launch {
            try {
                repository.insertDosis(dosis)
                Toast.makeText(context, "✅ Dosis agregada correctamente", Toast.LENGTH_SHORT).show()
            } catch (e: IllegalStateException) {
                Toast.makeText(context, "⚠️ ${e.message}", Toast.LENGTH_LONG).show()
            } catch (e: Exception) {
                Toast.makeText(context, "❌ Error: ${e.message}", Toast.LENGTH_LONG).show()
            }
        }
    }
    
    fun actualizarDosis(dosis: Dosis) {
        viewModelScope.launch {
            repository.updateDosis(dosis)
        }
    }
    
    fun eliminarDosis(dosis: Dosis) {
        viewModelScope.launch {
            repository.deleteDosis(dosis)
        }
    }
    
    fun limpiarHistorial() {
        viewModelScope.launch {
            repository.clearHistorial()
        }
    }
    
    fun limpiarTodasLasDosisESP32() {
        repository.limpiarTodasLasDosisESP32()
    }
    
    override fun onCleared() {
        super.onCleared()
        desconectar()
    }
}
