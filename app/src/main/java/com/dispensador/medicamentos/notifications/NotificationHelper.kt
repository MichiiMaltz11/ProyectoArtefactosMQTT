package com.dispensador.medicamentos.notifications

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import com.dispensador.medicamentos.MainActivity
import com.dispensador.medicamentos.R

class NotificationHelper(private val context: Context) {
    
    companion object {
        private const val CHANNEL_ID = "alarma_medicamentos"
        private const val CHANNEL_NAME = "Alarmas de Medicamentos"
        private const val CHANNEL_DESCRIPTION = "Notificaciones cuando es hora de tomar medicamento"
        private const val NOTIFICATION_ID = 1001
    }
    
    init {
        createNotificationChannel()
    }
    
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val importance = NotificationManager.IMPORTANCE_HIGH
            val channel = NotificationChannel(CHANNEL_ID, CHANNEL_NAME, importance).apply {
                description = CHANNEL_DESCRIPTION
                enableVibration(true)
                vibrationPattern = longArrayOf(0, 500, 200, 500)
            }
            
            val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }
    
    fun mostrarNotificacionAlarma(medicamento: String, compartimiento: Int, hora: String) {
        // Verificar permiso de notificaciones (Android 13+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ActivityCompat.checkSelfPermission(
                    context,
                    Manifest.permission.POST_NOTIFICATIONS
                ) != PackageManager.PERMISSION_GRANTED
            ) {
                return
            }
        }
        
        // Intent para abrir la app al tocar la notificación
        val intent = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val pendingIntent = PendingIntent.getActivity(
            context,
            0,
            intent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        
        // Construir la notificación
        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground) // Usa el ícono de la app
            .setContentTitle("⏰ Hora de tomar medicamento")
            .setContentText("$medicamento - Compartimento $compartimiento")
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .bigText("Es hora de tomar: $medicamento\nCompartimento: C$compartimiento\nHora programada: $hora")
            )
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setAutoCancel(true)
            .setContentIntent(pendingIntent)
            .setVibrate(longArrayOf(0, 500, 200, 500))
            .build()
        
        // Mostrar la notificación
        NotificationManagerCompat.from(context).notify(NOTIFICATION_ID, notification)
    }
    
    fun cancelarNotificacion() {
        NotificationManagerCompat.from(context).cancel(NOTIFICATION_ID)
    }
}
