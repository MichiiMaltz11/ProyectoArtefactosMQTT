package com.dispensador.medicamentos.mqtt

object MqttConfig {
    // IP de tu PC donde corre el broker Mosquitto (192.168.0.10)
    const val BROKER_URL = "tcp://192.168.137.179:1883"
    const val CLIENT_ID = "DispensadorMedicamentosApp"
    
    object Topics {
        // Topics para publicar (app -> ESP32)
        const val CONFIG_DOSIS = "/dispensador/config/dosis"
        const val CONFIG_HORA = "/config/hora"
        const val DETENER_ALARMA = "/alarma/detener"
        const val ELIMINAR_DOSIS = "/dispensador/config/eliminar"
        const val TEST_LED = "/test/led"
        const val TEST_BUZZER = "/test/buzzer"
        const val SOLICITAR_HISTORIAL = "/historial/solicitar"
        const val SOLICITAR_DOSIS = "/dispensador/solicitar/dosis"
        
        // Topics para suscribirse (ESP32 -> app)
        const val STATUS = "/dispensador/status"
        const val DOSIS_CONFIRMADA = "/dosis/confirmada"
        const val DOSIS_OMITIDA = "/dosis/omitida"
        const val ALARMA_ACTIVA = "/alarma/activa"
        const val HISTORIAL_DATOS = "/historial/datos"
        const val DOSIS_ACTUALES = "/dispensador/dosis/actuales"
    }
}
