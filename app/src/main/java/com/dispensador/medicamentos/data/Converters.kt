package com.dispensador.medicamentos.data

import androidx.room.TypeConverter

class Converters {
    @TypeConverter
    fun fromEstadoDosis(value: EstadoDosis): String {
        return value.name
    }
    
    @TypeConverter
    fun toEstadoDosis(value: String): EstadoDosis {
        return EstadoDosis.valueOf(value)
    }
}
