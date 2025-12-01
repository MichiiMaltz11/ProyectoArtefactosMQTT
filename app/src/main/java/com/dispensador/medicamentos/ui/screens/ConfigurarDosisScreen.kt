package com.dispensador.medicamentos.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccessTime
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.dispensador.medicamentos.data.Dosis
import com.dispensador.medicamentos.ui.viewmodel.DispensadorViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConfigurarDosisScreen(
    viewModel: DispensadorViewModel,
    modifier: Modifier = Modifier
) {
    var medicamento by remember { mutableStateOf("") }
    var horaTexto by remember { mutableStateOf("") } // Usuario escribe directamente "7:30" o "07:30"
    var esAM by remember { mutableStateOf(true) } // true = AM, false = PM
    var compartimiento by remember { mutableStateOf("1") }
    var showError by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf("") }
    
    // Función para validar formato de hora 12h (acepta "7:30" o "07:30")
    fun validarHora12(hora: String): Boolean {
        // Acepta formatos: 7:30, 07:30, 12:00, etc.
        val regex = Regex("^(0?[1-9]|1[0-2]):[0-5][0-9]$")
        return regex.matches(hora)
    }
    
    // Función para convertir 12h + AM/PM a formato 24h
    fun convertirA24h(hora12: String, esAM: Boolean): String {
        if (!validarHora12(hora12)) return ""
        
        val partes = hora12.split(":")
        if (partes.size != 2) return ""
        
        var hour = partes[0].toIntOrNull() ?: return ""
        val minute = partes[1]
        
        // Convertir a formato 24h
        hour = when {
            hour == 12 && esAM -> 0  // 12 AM = 00
            hour == 12 && !esAM -> 12 // 12 PM = 12
            !esAM -> hour + 12         // PM: sumar 12
            else -> hour               // AM: mantener
        }
        
        return String.format("%02d:%s", hour, minute)
    }
    
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Encabezado
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "Nueva Dosis",
                    style = MaterialTheme.typography.headlineMedium,
                    color = MaterialTheme.colorScheme.primary
                )
                Text(
                    text = "Programa tu medicamento",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            
            Icon(
                imageVector = Icons.Outlined.MedicalServices,
                contentDescription = null,
                modifier = Modifier.size(48.dp),
                tint = MaterialTheme.colorScheme.primary
            )
        }
        
        // Formulario en tarjeta
        ElevatedCard(
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                // Nombre del medicamento
                OutlinedTextField(
                    value = medicamento,
                    onValueChange = { medicamento = it },
                    label = { Text("Nombre del Medicamento") },
                    modifier = Modifier.fillMaxWidth(),
                    leadingIcon = {
                        Icon(Icons.Outlined.Medication, contentDescription = null)
                    },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.primary,
                        focusedLabelColor = MaterialTheme.colorScheme.primary
                    )
                )

                // Hora - Campo de texto simple (escribes directamente "7:30" o "07:30")
                OutlinedTextField(
                    value = horaTexto,
                    onValueChange = { input ->
                        // Solo permitir dígitos y ":" (máximo 5 caracteres)
                        val filtrado = input.filter { it.isDigit() || it == ':' }
                        if (filtrado.length <= 5) {
                            horaTexto = filtrado
                        }
                    },
                    label = { Text("Hora") },
                    placeholder = { Text("7:30 o 07:30") },
                    modifier = Modifier.fillMaxWidth(),
                    leadingIcon = {
                        Icon(Icons.Outlined.Schedule, contentDescription = null)
                    },
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Ascii,
                        autoCorrect = false
                    ),
                    singleLine = true,
                    supportingText = {
                        Text("Formato 12h con : (ej: 7:30 o 12:00)")
                    },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.primary,
                        focusedLabelColor = MaterialTheme.colorScheme.primary
                    )
                )

                // Botones AM/PM
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    FilterChip(
                        selected = esAM,
                        onClick = { esAM = true },
                        label = {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Icon(
                                    Icons.Outlined.WbSunny,
                                    contentDescription = null,
                                    modifier = Modifier.size(20.dp)
                                )
                                Text("AM (Mañana)", style = MaterialTheme.typography.titleSmall)
                            }
                        },
                        modifier = Modifier.weight(1f),
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
                            selectedLabelColor = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    )

                    FilterChip(
                        selected = !esAM,
                        onClick = { esAM = false },
                        label = {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Icon(
                                    Icons.Outlined.NightsStay,
                                    contentDescription = null,
                                    modifier = Modifier.size(20.dp)
                                )
                                Text("PM (Tarde)", style = MaterialTheme.typography.titleSmall)
                            }
                        },
                        modifier = Modifier.weight(1f),
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                            selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    )
                }

                // Selección de compartimiento
                ElevatedCard(
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Outlined.LocalPharmacy,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = "Selecciona el Compartimiento",
                                style = MaterialTheme.typography.titleMedium
                            )
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            (1..4).forEach { num ->
                                FilterChip(
                                    selected = compartimiento == num.toString(),
                                    onClick = { compartimiento = num.toString() },
                                    label = {
                                        Column(
                                            horizontalAlignment = Alignment.CenterHorizontally,
                                            modifier = Modifier.padding(vertical = 8.dp)
                                        ) {
                                            Icon(
                                                Icons.Outlined.Inventory,
                                                contentDescription = null,
                                                modifier = Modifier.size(20.dp)
                                            )
                                            Text(num.toString())
                                        }
                                    },
                                    modifier = Modifier.weight(1f),
                                    colors = FilterChipDefaults.filterChipColors(
                                        selectedContainerColor = MaterialTheme.colorScheme.primaryContainer
                                    )
                                )
                            }
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            (5..8).forEach { num ->
                                FilterChip(
                                    selected = compartimiento == num.toString(),
                                    onClick = { compartimiento = num.toString() },
                                    label = {
                                        Column(
                                            horizontalAlignment = Alignment.CenterHorizontally,
                                            modifier = Modifier.padding(vertical = 8.dp)
                                        ) {
                                            Icon(
                                                Icons.Outlined.Inventory,
                                                contentDescription = null,
                                                modifier = Modifier.size(20.dp)
                                            )
                                            Text(num.toString())
                                        }
                                    },
                                    modifier = Modifier.weight(1f),
                                    colors = FilterChipDefaults.filterChipColors(
                                        selectedContainerColor = MaterialTheme.colorScheme.primaryContainer
                                    )
                                )
                            }
                        }
                    }
                }

                // Información de ayuda
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.secondaryContainer
                    )
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Outlined.Info,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(25.dp)
                        )
                        Text(
                            text = "💊 Instrucciones:",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSecondaryContainer,
                            textAlign = TextAlign.Center
                        )
                        Text(
                            text = "• Ingresa el nombre del medicamento\n" +
                                    "• Escribe la hora\n" +
                                    "• Selecciona AM o PM\n" +
                                    "• Elige el compartimiento (1-8)",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSecondaryContainer,
                            textAlign = TextAlign.Center
                        )
                    }
                }

                if (showError) {
                    Card(
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.errorContainer
                        )
                    ) {
                        Text(
                            text = errorMessage,
                            modifier = Modifier.padding(16.dp),
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
                    }
                }

                Spacer(modifier = Modifier.weight(1f))

                // Botón de guardar
                FilledTonalButton(
                    onClick = {
                        // Validar campos
                        when {
                            medicamento.isBlank() -> {
                                showError = true
                                errorMessage = "Ingresa el nombre del medicamento"
                            }

                            !validarHora12(horaTexto) -> {
                                showError = true
                                errorMessage =
                                    "Hora inválida. Usa formato 7:30 o 07:30 (1-12 horas)"
                            }

                            else -> {
                                val hora24 = convertirA24h(horaTexto, esAM)
                                if (hora24.isEmpty()) {
                                    showError = true
                                    errorMessage = "Error al convertir la hora"
                                } else {
                                    showError = false
                                    val nuevaDosis = Dosis(
                                        hora = hora24,
                                        compartimiento = compartimiento.toInt(),
                                        medicamento = medicamento,
                                        activo = true
                                    )
                                    viewModel.agregarDosis(nuevaDosis)

                                    // Limpiar campos
                                    medicamento = ""
                                    horaTexto = ""
                                    esAM = true
                                    compartimiento = "1"
                                }
                            }
                        }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp),
                    enabled = viewModel.connectionState.collectAsState().value,
                    colors = ButtonDefaults.filledTonalButtonColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        contentColor = MaterialTheme.colorScheme.onPrimary
                    )
                ) {
                    Icon(
                        imageVector = Icons.Outlined.Save,
                        contentDescription = null
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        "Guardar Dosis",
                        style = MaterialTheme.typography.titleMedium
                    )
                }

                if (!viewModel.connectionState.collectAsState().value) {
                    Text(
                        text = "Conéctate al broker MQTT para guardar",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error,
                        modifier = Modifier.align(Alignment.CenterHorizontally)
                    )
                }
            }
        }
    }
}
