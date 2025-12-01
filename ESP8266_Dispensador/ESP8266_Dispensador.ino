#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Stepper.h>
#include <EEPROM.h>

// ====================== CONFIGURACIÓN WI-FI ======================
const char* ssid = "L6";           // Nombre de tu red WiFi
const char* password = "876543211";         //  Contraseña de tu WiFi

// ====================== CONFIGURACIÓN MQTT ======================
const char* mqtt_server = "192.168.137.179";  //  IP donde corre Mosquitto
const int mqtt_port = 1883;                //  Puerto MQTT estándar
WiFiClient espClient;
PubSubClient client(espClient);

// ====================== LCD ======================
// ESP8266: GPIO4=SDA (D2), GPIO5=SCL (D1)
#define SDA_PIN 4  // D2
#define SCL_PIN 5  // D1
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ====================== PINES ESP8266 ======================
// NodeMCU Pin Mapping:
#define LED_PIN D6      // GPIO12
#define BUTTON_PIN D7   // GPIO13 (con pull-up)
#define BUZZER_PIN D8   // GPIO15
#define IN1 D0          // GPIO16
#define IN2 D3          // GPIO0
#define IN3 D4          // GPIO2
#define IN4 D5          // GPIO14

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
  char hora[6];       // Formato "HH:MM" + null terminator
  int compartimiento; // 1-8
  char medicamento[20]; // Nombre limitado a 19 chars
  bool procesada_hoy; // Evita activaciones múltiples
};

#define MAX_DOSIS 8
Dosis dosis_programadas[MAX_DOSIS];
int total_dosis = 0;

// ====================== COLA DE EVENTOS PENDIENTES ======================
// 💾 Sistema de persistencia para eventos que no se enviaron por falta de conexión
struct EventoPendiente {
  bool activo;
  char tipo[10];          // "confirmada" o "omitida"
  char medicamento[20];
  int compartimiento;
  char hora[6];
  unsigned long timestamp;
  int intentos;
};

#define MAX_EVENTOS_PENDIENTES 5
EventoPendiente eventos_pendientes[MAX_EVENTOS_PENDIENTES];
int total_eventos_pendientes = 0;

// Direcciones EEPROM para eventos pendientes (después de las dosis)
#define EEPROM_ADDR_EVENTOS 280  // Después de 8 dosis (264 bytes usados)
#define EEPROM_ADDR_TOTAL_EVENTOS 480  // Al final del EEPROM

// ====================== ESTRUCTURA DE HISTORIAL ======================
// ❌ ELIMINADO: El ESP8266 NO guarda historial en memoria
// La app Android (Room DB) es la única fuente de verdad para el historial
// ✅ NUEVO: Cola de eventos pendientes se guarda en EEPROM para reintentar envío

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
NTPClient timeClient(ntpUDP, "pool.ntp.org", -21600, 60000); // GMT-6

// ====================== EEPROM (PERSISTENCIA) ======================
// ESP8266 usa EEPROM en lugar de Preferences
#define EEPROM_SIZE 512
#define EEPROM_ADDR_TOTAL_DOSIS 0
#define EEPROM_ADDR_POSICION 4
#define EEPROM_ADDR_DOSIS 8  // Cada dosis ocupa ~40 bytes

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
  
  Serial.printf("🔄 Motor: C%d → C%d (%d slots = %d pasos, error: %.2f)\n", 
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
  EEPROM.put(EEPROM_ADDR_POSICION, posicion_actual);
  EEPROM.commit();
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
                dosis.medicamento, dosis.compartimiento);
  
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
  lcd.print(String(dosis.medicamento).substring(0, 16));
  
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
  ultimo_cambio_lcd = 0;
  ultimo_beep = 0;
  tiempo_inicio_alarma = 0;
  tiempo_ultimo_intento = 0;
  
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
  
  Serial.printf("🔄 Reintento %d/%d\n", intentos_alarma, MAX_INTENTOS);
  
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

// =====================================================================
// =================== GESTIÓN DE DOSIS Y HORARIOS =====================
// =====================================================================

void agregarDosis(String hora, int compartimiento, String medicamento) {
  if (total_dosis >= MAX_DOSIS) {
    Serial.println("No se pueden agregar más dosis (máximo 8)");
    return;
  }
  
  // Validar que no haya alarma activa
  if (alarma_activa) {
    Serial.println("No se puede agregar dosis durante alarma activa");
    return;
  }
  
  dosis_programadas[total_dosis].activo = true;
  hora.toCharArray(dosis_programadas[total_dosis].hora, 6);
  dosis_programadas[total_dosis].compartimiento = compartimiento;
  medicamento.toCharArray(dosis_programadas[total_dosis].medicamento, 20);
  dosis_programadas[total_dosis].procesada_hoy = false;
  
  total_dosis++;
  
  Serial.printf("Dosis agregada: %s | C%d | %s (Total: %d)\n", 
                hora.c_str(), compartimiento, medicamento.c_str(), total_dosis);
  
  guardarDosisEnMemoria();
  enviarDosisActuales();
}

void eliminarDosis(int index) {
  if (index < 0 || index >= total_dosis) return;
  
  // Mover todas las dosis siguientes una posición atrás
  for (int i = index; i < total_dosis - 1; i++) {
    dosis_programadas[i] = dosis_programadas[i + 1];
  }
  
  total_dosis--;
  Serial.printf("Dosis eliminada (quedan %d)\n", total_dosis);
  
  guardarDosisEnMemoria();
}

void limpiarTodasLasDosis() {
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
  // ⚠️ ESP8266: Historial se envía inmediatamente a la app vía MQTT
  // NO se guarda en EEPROM por limitación de espacio (512 bytes total)
  // La app Room DB es la única fuente de verdad para el historial
  
  Serial.printf("📝 Registro creado: %s | C%d | %s\n", 
                medicamento.c_str(), compartimiento, estado.c_str());
  Serial.println("ℹ️ Este registro ya fue enviado a la app vía MQTT en dosisConfirmada/Omitida");
}

// =====================================================================
// ================ GESTIÓN DE EVENTOS PENDIENTES ======================
// =====================================================================

void guardarEventosPendientes() {
  EEPROM.put(EEPROM_ADDR_TOTAL_EVENTOS, total_eventos_pendientes);
  
  int addr = EEPROM_ADDR_EVENTOS;
  for (int i = 0; i < total_eventos_pendientes && i < MAX_EVENTOS_PENDIENTES; i++) {
    EEPROM.put(addr, eventos_pendientes[i]);
    addr += sizeof(EventoPendiente);
  }
  
  EEPROM.commit();
  Serial.printf("💾 %d eventos pendientes guardados en EEPROM\n", total_eventos_pendientes);
}

void cargarEventosPendientes() {
  EEPROM.get(EEPROM_ADDR_TOTAL_EVENTOS, total_eventos_pendientes);
  
  // Validar
  if (total_eventos_pendientes < 0 || total_eventos_pendientes > MAX_EVENTOS_PENDIENTES) {
    total_eventos_pendientes = 0;
    return;
  }
  
  int addr = EEPROM_ADDR_EVENTOS;
  for (int i = 0; i < total_eventos_pendientes; i++) {
    EEPROM.get(addr, eventos_pendientes[i]);
    addr += sizeof(EventoPendiente);
  }
  
  Serial.printf("📥 %d eventos pendientes cargados de EEPROM\n", total_eventos_pendientes);
}

void agregarEventoPendiente(String tipo, String medicamento, int compartimiento, 
                           String hora, unsigned long timestamp, int intentos) {
  if (total_eventos_pendientes >= MAX_EVENTOS_PENDIENTES) {
    Serial.println("⚠️ Cola de eventos llena, descartando el más antiguo");
    // Mover todos un espacio hacia atrás (FIFO)
    for (int i = 0; i < MAX_EVENTOS_PENDIENTES - 1; i++) {
      eventos_pendientes[i] = eventos_pendientes[i + 1];
    }
    total_eventos_pendientes = MAX_EVENTOS_PENDIENTES - 1;
  }
  
  // Agregar nuevo evento al final
  eventos_pendientes[total_eventos_pendientes].activo = true;
  tipo.toCharArray(eventos_pendientes[total_eventos_pendientes].tipo, 10);
  medicamento.toCharArray(eventos_pendientes[total_eventos_pendientes].medicamento, 20);
  eventos_pendientes[total_eventos_pendientes].compartimiento = compartimiento;
  hora.toCharArray(eventos_pendientes[total_eventos_pendientes].hora, 6);
  eventos_pendientes[total_eventos_pendientes].timestamp = timestamp;
  eventos_pendientes[total_eventos_pendientes].intentos = intentos;
  
  total_eventos_pendientes++;
  
  Serial.printf("📝 Evento pendiente agregado: %s - C%d (Total: %d)\n", 
                tipo.c_str(), compartimiento, total_eventos_pendientes);
  
  // Guardar en EEPROM inmediatamente
  guardarEventosPendientes();
}

void enviarEventosPendientes() {
  if (total_eventos_pendientes == 0) return;
  if (!client.connected()) {
    Serial.println("⚠️ No se pueden enviar eventos: MQTT desconectado");
    return;
  }
  
  Serial.printf("📤 Intentando enviar %d eventos pendientes...\n", total_eventos_pendientes);
  
  int enviados = 0;
  for (int i = 0; i < total_eventos_pendientes; i++) {
    if (!eventos_pendientes[i].activo) continue;
    
    String tipo = String(eventos_pendientes[i].tipo);
    String topic = (tipo == "confirmada") ? "/dosis/confirmada" : "/dosis/omitida";
    
    StaticJsonDocument<300> doc;
    doc["dosis_id"] = 0;
    doc["medicamento"] = eventos_pendientes[i].medicamento;
    doc["compartimiento"] = eventos_pendientes[i].compartimiento;
    doc["hora"] = eventos_pendientes[i].hora;
    doc["timestamp"] = eventos_pendientes[i].timestamp;
    doc["estado"] = tipo;
    doc["intentos"] = eventos_pendientes[i].intentos;
    doc["reintento"] = true;  // Marcar como reintento
    
    char buffer[300];
    serializeJson(doc, buffer);
    
    if (client.publish(topic.c_str(), buffer)) {
      Serial.printf("✅ Evento pendiente enviado: %s - C%d\n", 
                    tipo.c_str(), eventos_pendientes[i].compartimiento);
      eventos_pendientes[i].activo = false;
      enviados++;
    } else {
      Serial.printf("❌ No se pudo enviar evento: %s - C%d\n", 
                    tipo.c_str(), eventos_pendientes[i].compartimiento);
      break; // Si falla, detener y reintentar después
    }
    
    delay(200); // Pequeño delay entre envíos
  }
  
  if (enviados > 0) {
    // Compactar array eliminando eventos enviados
    int nuevoTotal = 0;
    for (int i = 0; i < total_eventos_pendientes; i++) {
      if (eventos_pendientes[i].activo) {
        eventos_pendientes[nuevoTotal] = eventos_pendientes[i];
        nuevoTotal++;
      }
    }
    total_eventos_pendientes = nuevoTotal;
    
    // Actualizar EEPROM
    guardarEventosPendientes();
    
    Serial.printf("✅ %d eventos enviados, %d pendientes\n", enviados, total_eventos_pendientes);
  }
}

// =====================================================================
// ================== VERIFICACIÓN Y CONFIRMACIÓN ======================
// =====================================================================

void dosisConfirmada() {
  if (!alarma_activa || dosis_actual_index < 0) return;
  
  // IMPORTANTE: Guardar datos ANTES de eliminar la dosis
  String medicamento_guardado = String(dosis_programadas[dosis_actual_index].medicamento);
  int compartimiento_guardado = dosis_programadas[dosis_actual_index].compartimiento;
  String hora_guardada = String(dosis_programadas[dosis_actual_index].hora);
  
  dosis_programadas[dosis_actual_index].procesada_hoy = true;
  
  unsigned long timestamp = timeClient.getEpochTime();
  
  // Guardar en historial local del ESP8266
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
  
  bool enviado = client.publish("/dosis/confirmada", buffer);
  
  if (!enviado) {
    Serial.println("⚠️ MQTT desconectado, guardando evento en cola...");
    agregarEventoPendiente("confirmada", medicamento_guardado, compartimiento_guardado, 
                          hora_guardada, timestamp, intentos_alarma);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DOSIS TOMADA!");
    lcd.setCursor(0, 1);
    lcd.print("(Envio pend.)");
  } else {
    Serial.println("✅ Evento confirmada enviado inmediatamente");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DOSIS TOMADA!");
    lcd.setCursor(0, 1);
    lcd.print("Bien hecho! :)");
  }
  
  // Feedback visual adicional ya mostrado arriba
  
  // Sonido de éxito
  tone(BUZZER_PIN, 1000, 200);
  delay(250);
  tone(BUZZER_PIN, 1500, 200);
  delay(250);
  tone(BUZZER_PIN, 2000, 400);
  
  Serial.printf("✅ Dosis confirmada: C%d - %s\n", compartimiento_guardado, medicamento_guardado.c_str());
  
  delay(3000);
  
  // IMPORTANTE: Eliminar la dosis del ESP8266 ANTES de desactivar alarma
  eliminarDosis(dosis_actual_index);
  Serial.println("Dosis eliminada del ESP8266 (ya fue tomada)");
  
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
  String medicamento_guardado = String(dosis_programadas[dosis_actual_index].medicamento);
  int compartimiento_guardado = dosis_programadas[dosis_actual_index].compartimiento;
  String hora_guardada = String(dosis_programadas[dosis_actual_index].hora);
  
  dosis_programadas[dosis_actual_index].procesada_hoy = true;
  
  unsigned long timestamp = timeClient.getEpochTime();
  
  // Guardar en historial local del ESP8266
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
  
  bool enviado = client.publish("/dosis/omitida", buffer);
  
  if (!enviado) {
    Serial.println("⚠️ MQTT desconectado, guardando evento en cola...");
    agregarEventoPendiente("omitida", medicamento_guardado, compartimiento_guardado, 
                          hora_guardada, timestamp, MAX_INTENTOS);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DOSIS NO TOMADA");
    lcd.setCursor(0, 1);
    lcd.print("(Envio pend.)");
  } else {
    Serial.println("✅ Evento omitida enviado inmediatamente");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DOSIS NO TOMADA");
    lcd.setCursor(0, 1);
    lcd.print("Consulta medico");
  }
  
  Serial.printf("⚠️ Dosis omitida: C%d - %s\n", compartimiento_guardado, medicamento_guardado.c_str());
  
  // Feedback visual adicional ya mostrado arriba
  
  // Sonido de advertencia
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 500, 300);
    delay(400);
  }
  
  delay(4000);
  
  // IMPORTANTE: Eliminar la dosis del ESP8266 ANTES de desactivar alarma
  eliminarDosis(dosis_actual_index);
  Serial.println("Dosis eliminada del ESP8266 (no fue tomada después de 3 intentos)");
  
  // Resetear índice ANTES de desactivar alarma
  dosis_actual_index = -1;
  
  desactivarAlarma();
  
  // Enviar lista actualizada de dosis a la app para sincronizar el dashboard
  delay(500);
  enviarDosisActuales();
  Serial.println("Lista de dosis actualizada enviada a la app");
}

void verificarHoraDosis() {
  if (alarma_activa) return; // No verificar si ya hay alarma activa
  
  // Obtener hora actual
  String hora_actual = timeClient.getFormattedTime().substring(0, 5); // "HH:MM"
  
  // Verificar si cambió de día (resetear flags a medianoche)
  String dia_actual = timeClient.getFormattedDate().substring(0, 10); // "YYYY-MM-DD"
  if (dia_actual != ultimo_dia_procesado) {
    resetearDosisDelDia();
    ultimo_dia_procesado = dia_actual;
  }
  
  // Buscar dosis que coincidan con hora actual
  for (int i = 0; i < total_dosis; i++) {
    if (dosis_programadas[i].activo && 
        !dosis_programadas[i].procesada_hoy && 
        String(dosis_programadas[i].hora) == hora_actual) {
      
      activarAlarma(i);
      break; // Solo una alarma a la vez
    }
  }
}

// =====================================================================
// ===================== PERSISTENCIA EEPROM ===========================
// =====================================================================

void guardarDosisEnMemoria() {
  EEPROM.put(EEPROM_ADDR_TOTAL_DOSIS, total_dosis);
  EEPROM.put(EEPROM_ADDR_POSICION, posicion_actual);
  
  int addr = EEPROM_ADDR_DOSIS;
  for (int i = 0; i < total_dosis; i++) {
    EEPROM.put(addr, dosis_programadas[i]);
    addr += sizeof(Dosis);
  }
  
  EEPROM.commit();
  Serial.println("Dosis y posición del motor guardadas en EEPROM");
}

void cargarDosisDeMemoria() {
  EEPROM.get(EEPROM_ADDR_TOTAL_DOSIS, total_dosis);
  EEPROM.get(EEPROM_ADDR_POSICION, posicion_actual);
  
  // Validar valores
  if (total_dosis < 0 || total_dosis > MAX_DOSIS) total_dosis = 0;
  if (posicion_actual < 1 || posicion_actual > 8) posicion_actual = 1;
  
  int addr = EEPROM_ADDR_DOSIS;
  for (int i = 0; i < total_dosis; i++) {
    EEPROM.get(addr, dosis_programadas[i]);
    dosis_programadas[i].procesada_hoy = false;
    addr += sizeof(Dosis);
  }
  
  Serial.printf("%d dosis cargadas de EEPROM. Motor en posición C%d\n", total_dosis, posicion_actual);
}

// =====================================================================
// ================ GESTIÓN DE EVENTOS PENDIENTES ======================
// =====================================================================

void guardarEventosPendientes() {
  EEPROM.put(EEPROM_ADDR_TOTAL_EVENTOS, total_eventos_pendientes);
  
  int addr = EEPROM_ADDR_EVENTOS;
  for (int i = 0; i < total_eventos_pendientes && i < MAX_EVENTOS_PENDIENTES; i++) {
    EEPROM.put(addr, eventos_pendientes[i]);
    addr += sizeof(EventoPendiente);
  }
  
  EEPROM.commit();
  Serial.printf("💾 %d eventos pendientes guardados en EEPROM\n", total_eventos_pendientes);
}

void cargarEventosPendientes() {
  EEPROM.get(EEPROM_ADDR_TOTAL_EVENTOS, total_eventos_pendientes);
  
  // Validar
  if (total_eventos_pendientes < 0 || total_eventos_pendientes > MAX_EVENTOS_PENDIENTES) {
    total_eventos_pendientes = 0;
    return;
  }
  
  int addr = EEPROM_ADDR_EVENTOS;
  for (int i = 0; i < total_eventos_pendientes; i++) {
    EEPROM.get(addr, eventos_pendientes[i]);
    addr += sizeof(EventoPendiente);
  }
  
  Serial.printf("📥 %d eventos pendientes cargados de EEPROM\n", total_eventos_pendientes);
}

void agregarEventoPendiente(String tipo, String medicamento, int compartimiento, 
                           String hora, unsigned long timestamp, int intentos) {
  if (total_eventos_pendientes >= MAX_EVENTOS_PENDIENTES) {
    Serial.println("⚠️ Cola de eventos llena, descartando el más antiguo");
    // Mover todos un espacio hacia atrás (FIFO)
    for (int i = 0; i < MAX_EVENTOS_PENDIENTES - 1; i++) {
      eventos_pendientes[i] = eventos_pendientes[i + 1];
    }
    total_eventos_pendientes = MAX_EVENTOS_PENDIENTES - 1;
  }
  
  // Agregar nuevo evento al final
  eventos_pendientes[total_eventos_pendientes].activo = true;
  tipo.toCharArray(eventos_pendientes[total_eventos_pendientes].tipo, 10);
  medicamento.toCharArray(eventos_pendientes[total_eventos_pendientes].medicamento, 20);
  eventos_pendientes[total_eventos_pendientes].compartimiento = compartimiento;
  hora.toCharArray(eventos_pendientes[total_eventos_pendientes].hora, 6);
  eventos_pendientes[total_eventos_pendientes].timestamp = timestamp;
  eventos_pendientes[total_eventos_pendientes].intentos = intentos;
  
  total_eventos_pendientes++;
  
  Serial.printf("📝 Evento pendiente agregado: %s - C%d (Total: %d)\n", 
                tipo.c_str(), compartimiento, total_eventos_pendientes);
  
  // Guardar en EEPROM inmediatamente
  guardarEventosPendientes();
}

void enviarEventosPendientes() {
  if (total_eventos_pendientes == 0) return;
  
  Serial.printf("📤 Intentando enviar %d eventos pendientes...\n", total_eventos_pendientes);
  
  int enviados = 0;
  for (int i = 0; i < total_eventos_pendientes; i++) {
    if (!eventos_pendientes[i].activo) continue;
    
    String tipo = String(eventos_pendientes[i].tipo);
    String topic = (tipo == "confirmada") ? "/dosis/confirmada" : "/dosis/omitida";
    
    StaticJsonDocument<300> doc;
    doc["dosis_id"] = 0;
    doc["medicamento"] = eventos_pendientes[i].medicamento;
    doc["compartimiento"] = eventos_pendientes[i].compartimiento;
    doc["hora"] = eventos_pendientes[i].hora;
    doc["timestamp"] = eventos_pendientes[i].timestamp;
    doc["estado"] = tipo;
    doc["intentos"] = eventos_pendientes[i].intentos;
    doc["reintento"] = true;  // Marcar como reintento
    
    char buffer[300];
    serializeJson(doc, buffer);
    
    if (client.publish(topic.c_str(), buffer)) {
      Serial.printf("✅ Evento pendiente enviado: %s - C%d\n", 
                    tipo.c_str(), eventos_pendientes[i].compartimiento);
      eventos_pendientes[i].activo = false;
      enviados++;
    } else {
      Serial.printf("❌ No se pudo enviar evento: %s - C%d\n", 
                    tipo.c_str(), eventos_pendientes[i].compartimiento);
      break; // Si falla, detener y reintentar después
    }
    
    delay(200); // Pequeño delay entre envíos
  }
  
  if (enviados > 0) {
    // Compactar array eliminando eventos enviados
    int nuevoTotal = 0;
    for (int i = 0; i < total_eventos_pendientes; i++) {
      if (eventos_pendientes[i].activo) {
        eventos_pendientes[nuevoTotal] = eventos_pendientes[i];
        nuevoTotal++;
      }
    }
    total_eventos_pendientes = nuevoTotal;
    
    // Actualizar EEPROM
    guardarEventosPendientes();
    
    Serial.printf("✅ %d eventos enviados, %d pendientes\n", enviados, total_eventos_pendientes);
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
  
  Serial.printf("📤 Dosis actuales enviadas: %d dosis\n", total_dosis);
}

// ❌ FUNCIÓN ELIMINADA: enviarHistorialCompleto()
// El ESP8266 NO envía historial porque no lo guarda en memoria
// Cuando confirma/omite una dosis, envía el evento vía /dosis/confirmada o /dosis/omitida
// La app Android recibe esos eventos y los guarda en Room DB

// =====================================================================
// ====================== CALLBACK MQTT ================================
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
      
      Serial.println("⏸️ Alarma silenciada. LED sigue parpadeando. Esperando 1 minuto antes del próximo reintento...");
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
    Serial.println("⚠️ Topic /historial/solicitar ignorado (ESP8266 no guarda historial)");
    Serial.println("ℹ️ El historial completo está en la app Android (Room DB)");
  }
  
  // ==================== SOLICITAR DOSIS ACTUALES ====================
  else if (topico == "/dispensador/solicitar/dosis") {
    Serial.println("📤 App solicitó dosis actuales, enviando...");
    enviarDosisActuales();
  }
}

// =====================================================================
// =================== CONEXIÓN WIFI Y MQTT ============================
// =====================================================================

void conectarWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");
  lcd.setCursor(0, 1);
  lcd.print(ssid);
  
  WiFi.begin(ssid, password);
  Serial.print("📡 Conectando a WiFi: ");
  Serial.println(ssid);
  Serial.println(WiFi.status());
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi conectado!");
    Serial.print("📡 IP: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\n❌ Error conectando WiFi");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error WiFi");
    delay(2000);
  }
}

void reconectarMQTT() {
  while (!client.connected()) {
    Serial.print("🔌 Conectando MQTT...");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Conectando MQTT");
    
    if (client.connect("ESP8266_Dispensador")) {
      Serial.println(" ✅ Conectado");
      
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
      doc["dispositivo"] = "ESP8266";
      doc["timestamp"] = timeClient.getEpochTime();
      
      char buffer[200];
      serializeJson(doc, buffer);
      client.publish("/dispensador/status", buffer);
      
      // 🆕 NUEVA ARQUITECTURA: Solo reportar estado, la app envía las dosis
      delay(500);
      Serial.println("📤 Reportando dosis activas a la app...");
      enviarDosisActuales();
      Serial.println("ℹ️ La app envía todas las dosis nuevamente si es necesario");
      
      // 💾 Enviar eventos pendientes si los hay
      delay(500);
      if (total_eventos_pendientes > 0) {
        Serial.println("🔄 Enviando eventos pendientes...");
        enviarEventosPendientes();
      }
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MQTT conectado!");
      delay(1500);
      mostrarPantallaInicio();
      
    } else {
      Serial.print(" ❌ Error, rc=");
      Serial.println(client.state());
      lcd.setCursor(0, 1);
      lcd.print("Error. Retry 3s");
      delay(3000);
    }
  }
}

// =====================================================================
// =================== PANTALLA LCD ====================================
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
// ======================== SETUP Y LOOP ===============================
// =====================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   DISPENSADOR DE MEDICAMENTOS ESP8266  ║");
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
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");
  lcd.setCursor(0, 1);
  lcd.print("Dispensador");
  delay(2000);
  
  // Inicializar EEPROM
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("📝 EEPROM inicializada (512 bytes)");
  
  // Conectar WiFi
  conectarWiFi();
  
  // Inicializar NTP
  timeClient.begin();
  timeClient.update();
  Serial.printf("🕐 Hora inicial: %s\n", timeClient.getFormattedTime().c_str());
  
  // Configurar MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(1024); // Buffer de 1KB para mensajes del historial
  
  // Cargar dosis guardadas (persistidas en EEPROM)
  cargarDosisDeMemoria();
  
  // Cargar eventos pendientes (si hubo desconexión previa)
  cargarEventosPendientes();
  if (total_eventos_pendientes > 0) {
    Serial.printf("⚠️ HAY %d EVENTOS PENDIENTES DE ENVIAR\n", total_eventos_pendientes);
  }
  
  // Conectar MQTT
  reconectarMQTT();
  
  // Inicializar motor
  motor.setSpeed(10);
  
  // Pantalla de inicio
  mostrarPantallaInicio();
  
  // Sonido de inicio
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  tone(BUZZER_PIN, 1500, 100);
  
  Serial.println("\n✅ Sistema inicializado correctamente!");
  Serial.println("📡 Esperando comandos MQTT...\n");
}

void loop() {
  // Mantener conexiones activas
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  
  if (!client.connected()) {
    reconectarMQTT();
  }
  client.loop();
  
  // Actualizar hora NTP cada minuto
  timeClient.update();
  
  // Verificar si cambió el día (resetear dosis procesadas)
  String dia_actual = String(timeClient.getEpochTime() / 86400);
  if (dia_actual != ultimo_dia_procesado) {
    resetearDosisDelDia();
    ultimo_dia_procesado = dia_actual;
    Serial.println("🌅 Nuevo día - Dosis reseteadas");
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
      Serial.println("⚠️ Índice de dosis inválido, desactivando alarma");
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
          lcd.print(String(dosis_programadas[dosis_actual_index].medicamento).substring(0, 16));
        }
        // No suena el buzzer durante el minuto de espera
        noTone(BUZZER_PIN);
      }
    }
  }
  
  // Verificar botón constantemente
  verificarBoton();
  
  // Verificar eventos pendientes cada 30 segundos si hay conexión MQTT
  static unsigned long ultimo_check_eventos = 0;
  if (client.connected() && total_eventos_pendientes > 0) {
    if (millis() - ultimo_check_eventos > 30000) {
      Serial.println("🔄 Verificando eventos pendientes...");
      enviarEventosPendientes();
      ultimo_check_eventos = millis();
    }
  }
  
  // Pequeño delay para no saturar el CPU
  delay(10);
  
  yield(); // Importante para ESP8266: evita watchdog reset
}
