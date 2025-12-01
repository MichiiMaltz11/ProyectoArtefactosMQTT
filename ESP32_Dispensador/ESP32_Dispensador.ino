#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Stepper.h>
#include <Preferences.h>

// ====================== CONFIGURACIÓN WI-FI ======================
const char* ssid = "L6";           // Nombre de tu red WiFi
const char* password = "876543211";         //  Contraseña de tu WiFi

// ====================== CONFIGURACIÓN MQTT ======================
const char* mqtt_server = "192.168.137.179";  //  IP donde corre Mosquitto
const int mqtt_port = 1883;                //  Puerto MQTT estándar
WiFiClient espClient;
PubSubClient client(espClient);

// ====================== LCD ======================
#define SDA_PIN 21
#define SCL_PIN 22
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ====================== PINES ======================
#define LED_PIN 14
#define BUTTON_PIN 27
#define BUZZER_PIN 33
#define IN1 13
#define IN2 12
#define IN3 32
#define IN4 25

// ====================== MOTOR PASO A PASO ======================
// Valores REALES del 28BYJ-48: 4076 pasos/vuelta completa (no 4096)
// En modo half-step: 2038 pasos/vuelta
#define STEPS_PER_REV 2038  // Valor real: 4076 / 2
// 2038 / 8 = 254.75 → Alternamos entre 254 y 255 para compensar
Stepper motor(STEPS_PER_REV, IN1, IN3, IN2, IN4);
int posicion_actual = 1;  // Compartimento actual del motor (1-8)
float error_acumulado = 0.0;  // Error de redondeo acumulado

// ====================== ESTRUCTURA DE DOSIS ======================
struct Dosis {
  bool activo;
  String hora;        // Formato "HH:MM"
  int compartimiento; // 1-8
  String medicamento;
  bool procesada_hoy; // Evita activaciones múltiples
};

#define MAX_DOSIS 8
Dosis dosis_programadas[MAX_DOSIS];
int total_dosis = 0;

// ====================== ESTRUCTURA DE HISTORIAL ======================
struct RegistroHistorial {
  unsigned long timestamp;     // Timestamp Unix
  String medicamento;          // Nombre del medicamento
  int compartimiento;          // 1-8
  String estado;               // "tomada" o "omitida"
  String hora_programada;      // Hora programada "HH:MM"
  int intentos;                // Número de intentos antes de tomar/omitir
};

#define MAX_HISTORIAL 100
RegistroHistorial historial[MAX_HISTORIAL];
int total_historial = 0;
int indice_historial = 0; // Índice circular para FIFO

// ====================== VARIABLES DE ALARMA ======================
bool alarma_activa = false;
int dosis_actual_index = -1;
int intentos_alarma = 0;
unsigned long tiempo_ultimo_intento = 0;
unsigned long tiempo_inicio_alarma = 0;
unsigned long ultimo_beep = 0;
unsigned long ultimo_cambio_lcd = 0; // Para actualizar LCD durante espera
const unsigned long INTERVALO_REINTENTO = 60000; // 1 minuto
const int MAX_INTENTOS = 3;
const unsigned long INTERVALO_BEEP = 5000; // Beep cada 5 segundos

// ====================== NTP CLIENT ======================
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -21600, 60000); // GMT-6 (Honduras)

// ====================== PREFERENCES (PERSISTENCIA) ======================
Preferences preferences;

// ====================== VARIABLES DE TIEMPO ======================
String ultimo_dia_procesado = "";
unsigned long ultimo_check_boton = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// =====================================================================
// ======================== FUNCIONES MOTOR ============================
// =====================================================================

void moverMotorACompartimiento(int compartimiento_destino) {
  if (compartimiento_destino < 1 || compartimiento_destino > 8) {
    Serial.println("Compartimiento inválido");
    return;
  }
  
  // Mostrar en LCD ANTES de mover el motor (mientras hay voltaje estable)
  lcd.clear();
  delay(10);
  lcd.setCursor(0, 0);
  lcd.print("Moviendo a C");
  lcd.print(compartimiento_destino);
  delay(100); // Dar tiempo al LCD antes de que el motor consuma corriente
  
  motor.setSpeed(10);
  
  // Calcular la diferencia
  int diferencia = compartimiento_destino - posicion_actual;
  
  // SIEMPRE AVANZAR HACIA ADELANTE (nunca retroceder)
  if (diferencia < 0) {
    diferencia += 8; // C8→C1 = -7, se convierte en +1 (dar la vuelta)
  } else if (diferencia == 0) {
    diferencia = 8; // Si ya estamos ahí, dar una vuelta completa para no perder posición
  }
  // Si diferencia > 0, avanzar normalmente
  
  // CORRECCIÓN DE ERROR ACUMULADO
  // Valor real: 254.75 pasos por compartimento (2038/8)
  // Alternamos entre 254 y 255 según el error acumulado
  int pasos_totales = 0;
  for (int i = 0; i < diferencia; i++) {
    error_acumulado += 0.75;  // 254.75 - 254 = 0.75 de error por slot
    
    int pasos_este_slot;
    if (error_acumulado >= 1.0) {
      pasos_este_slot = 255;  // Compensar con un paso extra
      error_acumulado -= 1.0;
    } else {
      pasos_este_slot = 254;  // Paso normal
    }
    
    pasos_totales += pasos_este_slot;
  }
  
  Serial.printf("Motor: C%d → C%d (%d slots = %d pasos, error: %.2f)\n", 
                posicion_actual, compartimiento_destino, diferencia, pasos_totales, error_acumulado);
  
  motor.step(pasos_totales);
  posicion_actual = compartimiento_destino;
  
  // Esperar a que el motor termine y la corriente se estabilice
  delay(200);
  
  // Re-inicializar el LCD después del movimiento (por si se corrompió)
  lcd.clear();
  delay(10);
  
  // Guardar la nueva posición en memoria
  guardarPosicionMotor();
  
  delay(500);
}

void guardarPosicionMotor() {
  preferences.begin("dispensador", false);
  preferences.putInt("pos_motor", posicion_actual);
  preferences.end();
}

// =====================================================================
// ======================== FUNCIONES ALARMA ===========================
// =====================================================================

void activarAlarma(int dosis_index) {
  if (dosis_index < 0 || dosis_index >= total_dosis) return;
  
  alarma_activa = true;
  dosis_actual_index = dosis_index;
  intentos_alarma = 1;
  tiempo_inicio_alarma = millis();
  tiempo_ultimo_intento = millis();
  
  Dosis& dosis = dosis_programadas[dosis_index];
  
  Serial.printf("ALARMA ACTIVADA - %s | C%d\n", 
                dosis.medicamento.c_str(), dosis.compartimiento);
  
  // Mover motor al compartimento
  moverMotorACompartimiento(dosis.compartimiento);
  
  // Activar LED y Buzzer
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 2000, 500); // Tono de 2000Hz por 500ms
  delay(600);
  tone(BUZZER_PIN, 2500, 500);
  delay(500); // Pequeño delay para estabilizar
  
  // Mostrar en LCD (limpiar y actualizar después del movimiento del motor)
  lcd.clear();
  delay(10); // Pequeño delay después de clear para evitar corrupción
  lcd.setCursor(0, 0);
  lcd.print("Hora medicina!");
  lcd.setCursor(0, 1);
  lcd.print(dosis.medicamento.substring(0, 16));
  
  // Publicar a MQTT
  StaticJsonDocument<200> doc;
  doc["estado"] = "activa";
  doc["dosis_id"] = dosis_index;
  doc["hora"] = dosis.hora;
  doc["compartimiento"] = dosis.compartimiento;
  doc["medicamento"] = dosis.medicamento;
  
  char buffer[200];
  serializeJson(doc, buffer);
  client.publish("/alarma/activa", buffer);
  
  // Parpadear LED
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(200);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
  }
}

void desactivarAlarma() {
  if (!alarma_activa) return;
  
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
  alarma_activa = false;
  intentos_alarma = 0;
  ultimo_cambio_lcd = 0; // Resetear variable de LCD
  ultimo_beep = 0; // Resetear beep
  tiempo_inicio_alarma = 0; // Resetear tiempo
  tiempo_ultimo_intento = 0; // Resetear intento
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Alarma");
  lcd.setCursor(0, 1);
  lcd.print("desactivada");
  
  Serial.println("Alarma desactivada - Regresando a pantalla de inicio");
  
  delay(2000);
  
  // Limpiar LCD completamente y mostrar pantalla de inicio
  lcd.clear();
  delay(200); // Asegurar que el LCD se limpie completamente
  mostrarPantallaInicio();
}

void reintentarAlarma() {
  if (!alarma_activa || dosis_actual_index < 0) return;
  
  intentos_alarma++;
  tiempo_ultimo_intento = millis();
  tiempo_inicio_alarma = millis(); // Reiniciar tiempo para el nuevo intento
  
  Serial.printf("Reintento %d/%d\n", intentos_alarma, MAX_INTENTOS);
  
  // Sonido más insistente al iniciar el intento
  tone(BUZZER_PIN, 2000, 300);
  delay(400);
  tone(BUZZER_PIN, 2500, 300);
  delay(400);
  tone(BUZZER_PIN, 3000, 500);
  
  digitalWrite(LED_PIN, HIGH);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RECORDATORIO!");
  lcd.setCursor(0, 1);
  lcd.print("Intento ");
  lcd.print(intentos_alarma);
  lcd.print("/");
  lcd.print(MAX_INTENTOS);
}

void dosisConfirmada() {
  if (!alarma_activa || dosis_actual_index < 0) return;
  
  // IMPORTANTE: Guardar datos ANTES de eliminar la dosis
  String medicamento_guardado = dosis_programadas[dosis_actual_index].medicamento;
  int compartimiento_guardado = dosis_programadas[dosis_actual_index].compartimiento;
  String hora_guardada = dosis_programadas[dosis_actual_index].hora;
  
  dosis_programadas[dosis_actual_index].procesada_hoy = true;
  
  unsigned long timestamp = timeClient.getEpochTime();
  
  // Guardar en historial local del ESP32
  agregarAlHistorial(
    medicamento_guardado,
    compartimiento_guardado,
    "tomada",
    hora_guardada,
    intentos_alarma,
    timestamp
  );
  
  // Publicar confirmación con datos guardados
  StaticJsonDocument<300> doc;
  doc["estado"] = "tomada";
  doc["dosis_id"] = dosis_actual_index;
  doc["hora_programada"] = hora_guardada;
  doc["hora_real"] = timeClient.getFormattedTime();
  doc["timestamp"] = timestamp;
  doc["compartimiento"] = compartimiento_guardado;
  doc["medicamento"] = medicamento_guardado;
  doc["intentos"] = intentos_alarma;
  
  char buffer[300];
  serializeJson(doc, buffer);
  client.publish("/dosis/confirmada", buffer);
  
  // Feedback visual
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DOSIS TOMADA!");
  lcd.setCursor(0, 1);
  lcd.print("Bien hecho! :)");
  
  // Sonido de éxito
  tone(BUZZER_PIN, 1000, 200);
  delay(250);
  tone(BUZZER_PIN, 1500, 200);
  delay(250);
  tone(BUZZER_PIN, 2000, 400);
  
  Serial.printf("Dosis confirmada: C%d - %s\n", compartimiento_guardado, medicamento_guardado.c_str());
  
  delay(3000);
  
  // IMPORTANTE: Eliminar la dosis del ESP32 ANTES de desactivar alarma
  eliminarDosis(dosis_actual_index);
  Serial.println("Dosis eliminada del ESP32 (ya fue tomada)");
  
  // Resetear índice ANTES de desactivar alarma
  dosis_actual_index = -1;
  
  desactivarAlarma();
  
  // Enviar lista actualizada de dosis a la app para sincronizar el dashboard
  delay(500);
  enviarDosisActuales();
  Serial.println("Lista de dosis actualizada enviada a la app");
}

void dosisOmitida() {
  if (!alarma_activa || dosis_actual_index < 0) return;
  
  // IMPORTANTE: Guardar datos ANTES de eliminar la dosis
  String medicamento_guardado = dosis_programadas[dosis_actual_index].medicamento;
  int compartimiento_guardado = dosis_programadas[dosis_actual_index].compartimiento;
  String hora_guardada = dosis_programadas[dosis_actual_index].hora;
  
  dosis_programadas[dosis_actual_index].procesada_hoy = true;
  
  unsigned long timestamp = timeClient.getEpochTime();
  
  // Guardar en historial local del ESP32
  agregarAlHistorial(
    medicamento_guardado,
    compartimiento_guardado,
    "omitida",
    hora_guardada,
    MAX_INTENTOS,
    timestamp
  );
  
  // Publicar omisión con datos guardados
  StaticJsonDocument<300> doc;
  doc["estado"] = "omitida";
  doc["dosis_id"] = dosis_actual_index;
  doc["hora_programada"] = hora_guardada;
  doc["timestamp"] = timestamp;
  doc["compartimiento"] = compartimiento_guardado;
  doc["medicamento"] = medicamento_guardado;
  doc["intentos_realizados"] = MAX_INTENTOS;
  
  char buffer[300];
  serializeJson(doc, buffer);
  client.publish("/dosis/omitida", buffer);
  
  Serial.printf("Dosis omitida: C%d - %s\n", compartimiento_guardado, medicamento_guardado.c_str());
  
  // Feedback visual de advertencia
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DOSIS NO TOMADA");
  lcd.setCursor(0, 1);
  lcd.print("Consulta medico");
  
  // Sonido de advertencia
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 500, 300);
    delay(400);
  }
  
  delay(4000);
  
  // IMPORTANTE: Eliminar la dosis del ESP32 ANTES de desactivar alarma
  eliminarDosis(dosis_actual_index);
  Serial.println("Dosis eliminada del ESP32 (no fue tomada después de 3 intentos)");
  
  // Resetear índice ANTES de desactivar alarma
  dosis_actual_index = -1;
  
  desactivarAlarma();
  
  // Enviar lista actualizada de dosis a la app para sincronizar el dashboard
  delay(500);
  enviarDosisActuales();
  Serial.println("Lista de dosis actualizada enviada a la app");
}

// =====================================================================
// ===================== FUNCIONES DE DOSIS ============================
// =====================================================================

void agregarDosis(String hora, int compartimiento, String medicamento) {
  if (total_dosis >= MAX_DOSIS) {
    Serial.println("No se pueden agregar más dosis (máximo 8)");
    return;
  }
  
  // Validar que no se agregue una dosis mientras hay una alarma activa
  if (alarma_activa) {
    Serial.println("No se puede agregar dosis mientras hay una alarma activa. Espere a confirmar o omitir la dosis actual.");
    return;
  }
  
  dosis_programadas[total_dosis].activo = true;
  dosis_programadas[total_dosis].hora = hora;
  dosis_programadas[total_dosis].compartimiento = compartimiento;
  dosis_programadas[total_dosis].medicamento = medicamento;
  dosis_programadas[total_dosis].procesada_hoy = false;
  
  total_dosis++;
  
  Serial.printf("Dosis agregada: %s | %s | C%d\n", 
                medicamento.c_str(), hora.c_str(), compartimiento);
  
  guardarDosisEnMemoria();
}

void eliminarDosis(int index) {
  if (index < 0 || index >= total_dosis) return;
  
  // Mover todas las dosis siguientes una posición atrás
  for (int i = index; i < total_dosis - 1; i++) {
    dosis_programadas[i] = dosis_programadas[i + 1];
  }
  
  total_dosis--;
  Serial.printf("Dosis eliminada (quedan %d)\n", total_dosis);
  
  // Limpiar la última posición de la memoria (la que ahora está vacía)
  preferences.begin("dispensador", false);
  String prefix = "dosis_" + String(total_dosis) + "_";
  preferences.remove((prefix + "activo").c_str());
  preferences.remove((prefix + "hora").c_str());
  preferences.remove((prefix + "comp").c_str());
  preferences.remove((prefix + "med").c_str());
  preferences.end();
  
  guardarDosisEnMemoria();
}

void limpiarTodasLasDosis() {
  // Limpiar todas las posiciones de memoria
  preferences.begin("dispensador", false);
  for (int i = 0; i < MAX_DOSIS; i++) {
    String prefix = "dosis_" + String(i) + "_";
    preferences.remove((prefix + "activo").c_str());
    preferences.remove((prefix + "hora").c_str());
    preferences.remove((prefix + "comp").c_str());
    preferences.remove((prefix + "med").c_str());
  }
  preferences.end();
  
  total_dosis = 0;
  Serial.println("Todas las dosis eliminadas de memoria");
  guardarDosisEnMemoria();
}

void resetearDosisDelDia() {
  for (int i = 0; i < total_dosis; i++) {
    dosis_programadas[i].procesada_hoy = false;
  }
  Serial.println("Dosis del día reseteadas");
}

// =====================================================================
// =================== FUNCIONES DE HISTORIAL ==========================
// =====================================================================

void agregarAlHistorial(String medicamento, int compartimiento, String estado, 
                        String hora_programada, int intentos, unsigned long timestamp) {
  
  // Guardar en array circular (FIFO)
  historial[indice_historial].timestamp = timestamp;
  historial[indice_historial].medicamento = medicamento;
  historial[indice_historial].compartimiento = compartimiento;
  historial[indice_historial].estado = estado;
  historial[indice_historial].hora_programada = hora_programada;
  historial[indice_historial].intentos = intentos;
  
  indice_historial = (indice_historial + 1) % MAX_HISTORIAL; // Circular
  
  if (total_historial < MAX_HISTORIAL) {
    total_historial++;
  }
  
  Serial.printf("Historial: %s | %s | C%d [Total: %d]", 
                estado.c_str(), medicamento.c_str(), compartimiento, total_historial);
  
  // Guardar en memoria Flash
  guardarHistorialEnMemoria();
}

void verificarHoraDosis() {
  if (alarma_activa) return; // No verificar si ya hay alarma activa
  
  String hora_actual = timeClient.getFormattedTime().substring(0, 5);
  
  for (int i = 0; i < total_dosis; i++) {
    Dosis& dosis = dosis_programadas[i];
    
    if (dosis.activo && 
        dosis.hora == hora_actual && 
        !dosis.procesada_hoy) {
      
      activarAlarma(i);
      break; // Solo una alarma a la vez
    }
  }
}

// =====================================================================
// =================== FUNCIONES DE PERSISTENCIA =======================
// =====================================================================

void guardarDosisEnMemoria() {
  preferences.begin("dispensador", false);
  preferences.putInt("total_dosis", total_dosis);
  preferences.putInt("pos_motor", posicion_actual); // Guardar posición del motor
  
  for (int i = 0; i < total_dosis; i++) {
    String prefix = "dosis_" + String(i) + "_";
    preferences.putBool((prefix + "activo").c_str(), dosis_programadas[i].activo);
    preferences.putString((prefix + "hora").c_str(), dosis_programadas[i].hora);
    preferences.putInt((prefix + "comp").c_str(), dosis_programadas[i].compartimiento);
    preferences.putString((prefix + "med").c_str(), dosis_programadas[i].medicamento);
  }
  
  preferences.end();
  Serial.println("Dosis y posición del motor guardadas en memoria");
}

void cargarDosisDeMemoria() {
  preferences.begin("dispensador", true);
  total_dosis = preferences.getInt("total_dosis", 0);
  posicion_actual = preferences.getInt("pos_motor", 1); // Cargar posición guardada
  
  for (int i = 0; i < total_dosis; i++) {
    String prefix = "dosis_" + String(i) + "_";
    dosis_programadas[i].activo = preferences.getBool((prefix + "activo").c_str(), false);
    dosis_programadas[i].hora = preferences.getString((prefix + "hora").c_str(), "00:00");
    dosis_programadas[i].compartimiento = preferences.getInt((prefix + "comp").c_str(), 1);
    dosis_programadas[i].medicamento = preferences.getString((prefix + "med").c_str(), "Medicamento");
    dosis_programadas[i].procesada_hoy = false;
  }
  
  preferences.end();
  Serial.printf("%d dosis cargadas de memoria. Motor en posición C%d\n", total_dosis, posicion_actual);
}

void guardarHistorialEnMemoria() {
  preferences.begin("historial", false);
  preferences.putInt("total", total_historial);
  preferences.putInt("indice", indice_historial);
  
  // Guardar solo los registros válidos de forma circular
  for (int i = 0; i < total_historial && i < MAX_HISTORIAL; i++) {
    String prefix = "h" + String(i) + "_";
    preferences.putULong((prefix + "ts").c_str(), historial[i].timestamp);
    preferences.putString((prefix + "med").c_str(), historial[i].medicamento);
    preferences.putInt((prefix + "comp").c_str(), historial[i].compartimiento);
    preferences.putString((prefix + "est").c_str(), historial[i].estado);
    preferences.putString((prefix + "hora").c_str(), historial[i].hora_programada);
    preferences.putInt((prefix + "int").c_str(), historial[i].intentos);
  }
  
  preferences.end();
}

void cargarHistorialDeMemoria() {
  preferences.begin("historial", true);
  total_historial = preferences.getInt("total", 0);
  indice_historial = preferences.getInt("indice", 0);
  
  for (int i = 0; i < total_historial && i < MAX_HISTORIAL; i++) {
    String prefix = "h" + String(i) + "_";
    historial[i].timestamp = preferences.getULong((prefix + "ts").c_str(), 0);
    historial[i].medicamento = preferences.getString((prefix + "med").c_str(), "Desconocido");
    historial[i].compartimiento = preferences.getInt((prefix + "comp").c_str(), 1);
    historial[i].estado = preferences.getString((prefix + "est").c_str(), "desconocido");
    historial[i].hora_programada = preferences.getString((prefix + "hora").c_str(), "00:00");
    historial[i].intentos = preferences.getInt((prefix + "int").c_str(), 1);
  }
  
  preferences.end();
  Serial.printf("%d registros de historial cargados\n", total_historial);
}

// =====================================================================
// ======================= CALLBACK MQTT ===============================
// =====================================================================

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String mensaje = String((char*)payload);
  String topico = String(topic);
  
  Serial.printf("MQTT: %s → %s\n", topic, mensaje.c_str());
  
  // ==================== PRUEBAS DE HARDWARE ====================
  if (topico == "/test/led") {
    bool encender = (mensaje == "on" || mensaje == "true" || mensaje == "1");
    digitalWrite(LED_PIN, encender ? HIGH : LOW);
    Serial.println(encender ? "LED ON" : "LED OFF");
  }
  else if (topico == "/test/buzzer") {
    bool encender = (mensaje == "on" || mensaje == "true" || mensaje == "1");
    if (encender) {
      tone(BUZZER_PIN, 2000);
      Serial.println("Buzzer ON");
    } else {
      noTone(BUZZER_PIN);
      Serial.println("Buzzer OFF");
    }
  }
  
  // ==================== CONTROL DE ALARMA ====================
  else if (topico == "/alarma/detener") {
    if (alarma_activa) {
      Serial.println("Comando: Silenciar alarma (saltar al minuto de espera)");
      
      // Silenciar SOLO el buzzer (el LED sigue parpadeando en el loop)
      noTone(BUZZER_PIN);
      
      // Saltar al minuto de espera (forzar que ya pasó el minuto activo)
      tiempo_inicio_alarma = millis() - INTERVALO_REINTENTO - 1000; // Forzar que ya pasó 1 minuto
      ultimo_beep = 0;
      ultimo_cambio_lcd = 0; // Resetear para que actualice LCD inmediatamente
      
      // Mostrar en LCD que está en espera
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Alarma silenciada");
      lcd.setCursor(0, 1);
      lcd.print("Dosis pendiente");
      
      Serial.println("Alarma silenciada. LED sigue parpadeando. Esperando 1 minuto antes del próximo reintento...");
    } else {
      Serial.println("No hay alarma activa para detener");
    }
  }
  
  // ==================== CONFIGURACIÓN DE DOSIS ====================
  else if (topico == "/dispensador/config/dosis") {
    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, mensaje);
    
    if (error) {
      Serial.println("Error parseando JSON de dosis");
      return;
    }
    
    String hora = doc["hora"] | "00:00";
    int compartimiento = doc["compartimiento"] | 1;
    String medicamento = doc["medicamento"] | "Medicina";
    
    agregarDosis(hora, compartimiento, medicamento);
    
    // Confirmar recepción
    StaticJsonDocument<200> respuesta;
    respuesta["status"] = "ok";
    respuesta["total_dosis"] = total_dosis;
    char buffer[200];
    serializeJson(respuesta, buffer);
    client.publish("/dispensador/config/confirmacion", buffer);
    
    // Mostrar en LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Nueva dosis!");
    lcd.setCursor(0, 1);
    lcd.print(hora);
    lcd.print(" C");
    lcd.print(compartimiento);
    delay(2000);
    mostrarPantallaInicio();
  }
  
  // ==================== ELIMINAR DOSIS ====================
  else if (topico == "/dispensador/config/eliminar") {
    StaticJsonDocument<100> doc;
    deserializeJson(doc, mensaje);
    int compartimiento = doc["compartimiento"] | -1;
    
    // Buscar dosis por compartimiento
    int index_a_eliminar = -1;
    for (int i = 0; i < total_dosis; i++) {
      if (dosis_programadas[i].compartimiento == compartimiento) {
        index_a_eliminar = i;
        break;
      }
    }
    
    if (index_a_eliminar >= 0) {
      Serial.printf("Eliminando dosis en compartimiento C%d (índice %d)\n", compartimiento, index_a_eliminar);
      eliminarDosis(index_a_eliminar);
      
      // Actualizar LCD inmediatamente
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Dosis eliminada");
      lcd.setCursor(0, 1);
      lcd.print("C");
      lcd.print(compartimiento);
      delay(1500);
      mostrarPantallaInicio();
      
      // Enviar confirmación de dosis actuales
      delay(100);
      enviarDosisActuales();
    } else {
      Serial.printf("No se encontró dosis en compartimiento C%d para eliminar\n", compartimiento);
    }
  }
  
  // ==================== SINCRONIZAR HORA ====================
  else if (topico == "/config/hora") {
    // Forzar actualización de NTP
    timeClient.forceUpdate();
    Serial.printf("Hora sincronizada con NTP: %s\n", timeClient.getFormattedTime().c_str());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hora sincronizada");
    lcd.setCursor(0, 1);
    lcd.print(timeClient.getFormattedTime());
    delay(2000);
    mostrarPantallaInicio();
  }
  
  // ==================== LIMPIAR HISTORIAL ====================
  else if (topico == "/dispensador/limpiar") {
    limpiarTodasLasDosis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Dosis borradas");
    delay(2000);
    mostrarPantallaInicio();
  }
  
  // ==================== SINCRONIZAR HISTORIAL ====================
  else if (topico == "/historial/solicitar") {
    Serial.println("Enviando historial completo a la app...");
    enviarHistorialCompleto();
  }
  
  // ==================== SOLICITAR DOSIS ACTUALES ====================
  else if (topico == "/dispensador/solicitar/dosis") {
    Serial.println("📤 App solicitó dosis actuales, enviando...");
    enviarDosisActuales();
  }
}

// =====================================================================
// =================== SINCRONIZACIÓN DE HISTORIAL =====================
// =====================================================================

void enviarDosisActuales() {
  StaticJsonDocument<1024> doc;
  JsonArray array = doc.createNestedArray("dosis");
  
  for (int i = 0; i < total_dosis; i++) {
    JsonObject dosis = array.createNestedObject();
    dosis["dosis_id"] = i;
    dosis["hora"] = dosis_programadas[i].hora;
    dosis["compartimiento"] = dosis_programadas[i].compartimiento;
    dosis["medicamento"] = dosis_programadas[i].medicamento;
  }
  
  doc["total"] = total_dosis;
  
  char buffer[1024];
  serializeJson(doc, buffer);
  client.publish("/dispensador/dosis/actuales", buffer);
  
  Serial.printf("Dosis actuales enviadas: %d dosis\n", total_dosis);
}

void enviarHistorialCompleto() {
  if (total_historial == 0) {
    // Enviar array vacío
    client.publish("/historial/datos", "[]");
    Serial.println("Historial vacío enviado");
    return;
  }
  
  // Enviar en bloques de 5 registros para evitar problemas de buffer MQTT
  const int REGISTROS_POR_BLOQUE = 5;
  int bloques_totales = (total_historial + REGISTROS_POR_BLOQUE - 1) / REGISTROS_POR_BLOQUE;
  
  for (int bloque = 0; bloque < bloques_totales; bloque++) {
    StaticJsonDocument<1024> doc;
    JsonArray array = doc.createNestedArray("registros");
    
    int inicio = bloque * REGISTROS_POR_BLOQUE;
    int fin = min(inicio + REGISTROS_POR_BLOQUE, total_historial);
    
    for (int i = inicio; i < fin; i++) {
      JsonObject registro = array.createNestedObject();
      registro["timestamp"] = historial[i].timestamp;
      registro["medicamento"] = historial[i].medicamento;
      registro["compartimiento"] = historial[i].compartimiento;
      registro["estado"] = historial[i].estado;
      registro["hora_programada"] = historial[i].hora_programada;
      registro["intentos"] = historial[i].intentos;
    }
    
    doc["bloque"] = bloque + 1;
    doc["total_bloques"] = bloques_totales;
    doc["total_registros"] = total_historial;
    
    char buffer[1024];
    serializeJson(doc, buffer);
    client.publish("/historial/datos", buffer);
    
    Serial.printf("Bloque %d/%d enviado (%d registros)\n", 
                  bloque + 1, bloques_totales, fin - inicio);
    
    delay(300); // Delay de 300ms entre bloques para evitar saturación
  }
  
  Serial.println("Historial completo sincronizado");
}

// =====================================================================
// ===================== FUNCIONES DE CONEXIÓN =========================
// =====================================================================

void conectarWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");
  lcd.setCursor(0, 1);
  lcd.print(ssid);
  
  WiFi.begin(ssid, password);
  Serial.println(WiFi.status());
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado!");
    Serial.print("📡 IP: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\nError conectando WiFi");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error WiFi");
    delay(2000);
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Conectando MQTT...");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Conectando MQTT");
    
    if (client.connect("ESP32_Dispensador")) {
      Serial.println("Conectado");
      
      // Suscribirse a todos los topics
      client.subscribe("/dispensador/config/dosis");
      client.subscribe("/dispensador/config/eliminar");
      client.subscribe("/dispensador/limpiar");
      client.subscribe("/config/hora");
      client.subscribe("/alarma/detener");
      client.subscribe("/test/led");
      client.subscribe("/test/buzzer");
      client.subscribe("/historial/solicitar");
      client.subscribe("/dispensador/solicitar/dosis");
      
      // Publicar estado online
      StaticJsonDocument<200> doc;
      doc["online"] = true;
      doc["ip"] = WiFi.localIP().toString();
      doc["total_dosis"] = total_dosis;
      doc["total_historial"] = total_historial;
      doc["timestamp"] = timeClient.getEpochTime();
      
      char buffer[200];
      serializeJson(doc, buffer);
      client.publish("/dispensador/status", buffer);
      
      // Auto-sincronizar historial al conectar
      delay(500);
      Serial.println("Auto-sincronizando historial...");
      enviarHistorialCompleto();
      
      // Enviar lista actual de dosis para sincronización
      delay(500);
      Serial.println("Enviando dosis actuales para sincronización...");
      enviarDosisActuales();
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MQTT conectado!");
      delay(1500);
      mostrarPantallaInicio();
      
    } else {
      Serial.print("Error, rc=");
      Serial.println(client.state());
      lcd.setCursor(0, 1);
      lcd.print("Error. Retry 3s");
      delay(3000);
    }
  }
}

// =====================================================================
// ===================== FUNCIONES DE LCD ==============================
// =====================================================================

void mostrarPantallaInicio() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dispensador ");
  lcd.print((char)0x01); // Símbolo de medicina
  lcd.setCursor(0, 1);
  
  if (total_dosis == 0) {
    lcd.print("Sin dosis");
  } else {
    lcd.print("Dosis: ");
    lcd.print(total_dosis);
    lcd.print(" | ");
    lcd.print(timeClient.getFormattedTime().substring(0, 5));
  }
}

void mostrarEstadoSistema() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi:");
  lcd.print(WiFi.status() == WL_CONNECTED ? "OK" : "NO");
  lcd.print(" MQTT:");
  lcd.print(client.connected() ? "OK" : "NO");
  lcd.setCursor(0, 1);
  lcd.print(timeClient.getFormattedTime());
  lcd.print(" D:");
  lcd.print(total_dosis);
}

// =====================================================================
// ===================== VERIFICACIÓN DE BOTÓN =========================
// =====================================================================

void verificarBoton() {
  if (millis() - ultimo_check_boton < DEBOUNCE_DELAY) return;
  
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(DEBOUNCE_DELAY); // Debounce
    
    if (digitalRead(BUTTON_PIN) == LOW) {
      ultimo_check_boton = millis();
      
      if (alarma_activa) {
        // Confirmar dosis
        dosisConfirmada();
      } else {
        // Mostrar estado del sistema
        mostrarEstadoSistema();
        delay(3000);
        mostrarPantallaInicio();
      }
      
      // Esperar a que suelte el botón
      while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
      }
    }
  }
}

// =====================================================================
// ========================== SETUP ====================================
// =====================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   DISPENSADOR DE MEDICAMENTOS ESP32    ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println();
  
  // Configurar pines
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(LED_PIN, LOW);
  noTone(BUZZER_PIN);
  
  // Inicializar LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");
  lcd.setCursor(0, 1);
  lcd.print("Dispensador");
  delay(2000);
  
  // Conectar WiFi
  conectarWiFi();
  
  // Inicializar NTP
  timeClient.begin();
  timeClient.update();
  Serial.printf("Hora inicial: %s\n", timeClient.getFormattedTime().c_str());
  
  // Configurar MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(1024); // Buffer de 1KB para mensajes del historial
  
  // Cargar dosis guardadas
  cargarDosisDeMemoria();
  
  // Cargar historial guardado
  cargarHistorialDeMemoria();
  
  // Conectar MQTT
  reconnectMQTT();
  
  // Inicializar motor
  motor.setSpeed(10);
  
  // Pantalla de inicio
  mostrarPantallaInicio();
  
  // Sonido de inicio
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  tone(BUZZER_PIN, 1500, 100);
  
  Serial.println("\nSistema inicializado correctamente!");
  Serial.println("Esperando comandos MQTT...\n");
}

// =====================================================================
// =========================== LOOP ====================================
// =====================================================================

void loop() {
  // Mantener conexiones activas
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Actualizar hora NTP cada minuto
  timeClient.update();
  
  // Verificar si cambió el día (resetear dosis procesadas)
  String dia_actual = String(timeClient.getEpochTime() / 86400);
  if (dia_actual != ultimo_dia_procesado) {
    resetearDosisDelDia();
    ultimo_dia_procesado = dia_actual;
    Serial.println("Nuevo día - Dosis reseteadas");
  }
  
  // Verificar si hay dosis pendientes
  if (!alarma_activa) {
    verificarHoraDosis();
  }
  
  // Gestión de alarma activa
  if (alarma_activa) {
    unsigned long tiempo_transcurrido = millis() - tiempo_ultimo_intento;
    unsigned long tiempo_desde_inicio_intento = millis() - tiempo_inicio_alarma;
    
    // Verificar si pasaron 2 minutos completos (1 min sonando + 1 min silencio)
    if (tiempo_transcurrido >= (INTERVALO_REINTENTO * 2)) {
      if (intentos_alarma < MAX_INTENTOS) {
        reintentarAlarma();
      } else {
        // Máximo de intentos alcanzado
        dosisOmitida();
      }
    }
    
    // Parpadear LED durante alarma (siempre activo)
    if ((millis() / 500) % 2 == 0) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    
    // Verificación adicional: si el índice es inválido, salir del modo alarma
    if (dosis_actual_index < 0 || dosis_actual_index >= total_dosis) {
      Serial.println("Índice de dosis inválido, desactivando alarma");
      alarma_activa = false;
      noTone(BUZZER_PIN);
      digitalWrite(LED_PIN, LOW);
      mostrarPantallaInicio();
    } else {
      // Sonar buzzer SOLO durante el primer minuto de cada intento
      if (tiempo_desde_inicio_intento < INTERVALO_REINTENTO) {
        // Dentro del minuto activo - sonar cada 5 segundos
        unsigned long tiempo_actual = millis();
        if (tiempo_actual - ultimo_beep >= INTERVALO_BEEP) {
          ultimo_beep = tiempo_actual;
          // Un solo beep corto
          tone(BUZZER_PIN, 2000, 200);
        }
      } else {
        // Minuto de espera - mostrar "Dosis pendiente" en LCD
        if (millis() - ultimo_cambio_lcd >= 2000) { // Actualizar LCD cada 2 segundos
          ultimo_cambio_lcd = millis();
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Dosis pendiente");
          lcd.setCursor(0, 1);
          lcd.print(dosis_programadas[dosis_actual_index].medicamento.substring(0, 16));
        }
        // No suena el buzzer durante el minuto de espera
        noTone(BUZZER_PIN);
      }
    }
  }
  
  // Verificar botón constantemente
  verificarBoton();
  
  // Pequeño delay para no saturar el CPU
  delay(10);
}
