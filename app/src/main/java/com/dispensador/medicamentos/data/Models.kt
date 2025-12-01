package com.dispensador.medicamentos.data

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "dosis")
data class Dosis(
    @PrimaryKey(autoGenerate = true)
    val id: Int = 0,
    val hora: String,          // Formato "HH:mm"
    val compartimiento: Int,    // 1-8
    val medicamento: String,
    val activo: Boolean = true
)

@Entity(tableName = "historial_dosis")
data class HistorialDosis(
    @PrimaryKey(autoGenerate = true)
    val id: Int = 0,
    val dosisId: Int,
    val timestamp: Long,
    val estado: EstadoDosis,
    val medicamento: String,
    val compartimiento: Int
)

enum class EstadoDosis {
    TOMADA,
    OMITIDA
}

// Mensaje JSON para configurar dosis en ESP32
data class ConfigDosisMessage(
    val dosis_id: Int,
    val hora: String,
    val compartimiento: Int,
    val medicamento: String,
    val activo: Boolean
)
