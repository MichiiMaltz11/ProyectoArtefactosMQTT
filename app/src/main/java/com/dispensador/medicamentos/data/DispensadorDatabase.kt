package com.dispensador.medicamentos.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.TypeConverters

@Database(
    entities = [Dosis::class, HistorialDosis::class],
    version = 1,
    exportSchema = false
)
@TypeConverters(Converters::class)
abstract class DispensadorDatabase : RoomDatabase() {
    abstract fun dosisDao(): DosisDao
    abstract fun historialDosisDao(): HistorialDosisDao
    
    companion object {
        @Volatile
        private var INSTANCE: DispensadorDatabase? = null
        
        fun getDatabase(context: Context): DispensadorDatabase {
            return INSTANCE ?: synchronized(this) {
                val instance = Room.databaseBuilder(
                    context.applicationContext,
                    DispensadorDatabase::class.java,
                    "dispensador_database"
                ).build()
                INSTANCE = instance
                instance
            }
        }
    }
}
