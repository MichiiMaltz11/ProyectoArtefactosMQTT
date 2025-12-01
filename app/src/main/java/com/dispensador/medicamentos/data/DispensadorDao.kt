package com.dispensador.medicamentos.data

import androidx.room.Dao
import androidx.room.Delete
import androidx.room.Insert
import androidx.room.Query
import androidx.room.Update
import kotlinx.coroutines.flow.Flow

@Dao
interface DosisDao {
    @Query("SELECT * FROM dosis ORDER BY hora ASC")
    fun getAllDosis(): Flow<List<Dosis>>
    
    @Query("SELECT * FROM dosis ORDER BY hora ASC")
    suspend fun getAllDosisList(): List<Dosis>
    
    @Query("SELECT * FROM dosis WHERE id = :id")
    suspend fun getDosisById(id: Int): Dosis?
    
    @Query("SELECT * FROM dosis WHERE compartimiento = :compartimiento LIMIT 1")
    suspend fun getDosisByCompartimiento(compartimiento: Int): Dosis?
    
    @Insert
    suspend fun insertDosis(dosis: Dosis): Long
    
    @Update
    suspend fun updateDosis(dosis: Dosis)
    
    @Delete
    suspend fun deleteDosis(dosis: Dosis)
}

@Dao
interface HistorialDosisDao {
    @Query("SELECT * FROM historial_dosis ORDER BY timestamp DESC")
    fun getAllHistorial(): Flow<List<HistorialDosis>>
    
    @Query("SELECT * FROM historial_dosis WHERE timestamp >= :startTime ORDER BY timestamp DESC")
    fun getHistorialDesde(startTime: Long): Flow<List<HistorialDosis>>
    
    @Query("SELECT * FROM historial_dosis WHERE timestamp = :timestamp LIMIT 1")
    suspend fun getHistorialByTimestamp(timestamp: Long): HistorialDosis?
    
    @Query("SELECT COUNT(*) FROM historial_dosis")
    suspend fun getHistorialCount(): Int
    
    @Query("SELECT * FROM historial_dosis ORDER BY timestamp ASC LIMIT 1")
    suspend fun getOldestHistorial(): HistorialDosis?
    
    @Delete
    suspend fun deleteHistorial(historial: HistorialDosis)
    
    @Insert
    suspend fun insertHistorial(historial: HistorialDosis)
    
    @Query("DELETE FROM historial_dosis")
    suspend fun clearHistorial()
}
