package com.dispensador.medicamentos.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.dispensador.medicamentos.ui.viewmodel.DispensadorViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AjustesScreen(
    viewModel: DispensadorViewModel,
    modifier: Modifier = Modifier
) {
    val connectionState by viewModel.connectionState.collectAsState()
    var ledEncendido by remember { mutableStateOf(false) }
    var buzzerEncendido by remember { mutableStateOf(false) }
    
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Header con icono
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(
                imageVector = Icons.Outlined.Settings,
                contentDescription = null,
                modifier = Modifier.size(32.dp),
                tint = MaterialTheme.colorScheme.primary
            )
            Column {
                Text(
                    text = "Ajustes",
                    style = MaterialTheme.typography.headlineMedium
                )
            }
        }
        

        // Sección de conexión
        Text(
            text = "Conexión MQTT",
            style = MaterialTheme.typography.titleLarge
        )
        
        ElevatedCard(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = if (connectionState)
                    MaterialTheme.colorScheme.primaryContainer
                else
                    MaterialTheme.colorScheme.errorContainer
            )
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = if (connectionState) Icons.Outlined.Wifi else Icons.Outlined.WifiOff,
                            contentDescription = null,
                            modifier = Modifier.size(40.dp),
                            tint = if (connectionState) 
                                MaterialTheme.colorScheme.primary 
                            else 
                                MaterialTheme.colorScheme.error
                        )
                        Column {
                            Text(
                                text = "Estado de Conexión",
                                style = MaterialTheme.typography.titleMedium
                            )
                            Text(
                                text = if (connectionState) "Conectado al broker" else "Desconectado",
                                style = MaterialTheme.typography.bodyMedium,
                                color = if (connectionState) 
                                    MaterialTheme.colorScheme.primary 
                                else 
                                    MaterialTheme.colorScheme.error
                            )
                        }
                    }
                    
                    FilledTonalButton(
                        onClick = {
                            if (connectionState) viewModel.desconectar()
                            else viewModel.conectar()
                        },
                        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                    ) {
                        Icon(
                            if (connectionState) Icons.Outlined.PowerSettingsNew else Icons.Outlined.Power,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = if (connectionState) "Desconectar" else "Conectar",
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }

            }
        }
        
        // Pruebas de hardware
        Text(
            text = "Pruebas",
            style = MaterialTheme.typography.titleLarge
        )
        
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Test LED
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(
                            Icons.Outlined.Lightbulb,
                            contentDescription = null,
                            modifier = Modifier.size(32.dp),
                            tint = if (ledEncendido) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Column {
                            Text(
                                text = "LED Indicador",
                                style = MaterialTheme.typography.titleMedium
                            )
                            Text(
                                text = if (ledEncendido) "💡 Encendido" else "🌑 Apagado",
                                style = MaterialTheme.typography.bodySmall,
                                color = if (ledEncendido) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    
                    Switch(
                        checked = ledEncendido,
                        onCheckedChange = { 
                            ledEncendido = it
                            viewModel.testLed(it)
                        },
                        enabled = connectionState
                    )
                }
                
                HorizontalDivider()
                
                // Test Buzzer
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(
                            Icons.Outlined.Notifications,
                            contentDescription = null,
                            modifier = Modifier.size(32.dp),
                            tint = if (buzzerEncendido) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Column {
                            Text(
                                text = "Buzzer Alarma",
                                style = MaterialTheme.typography.titleMedium
                            )
                            Text(
                                text = if (buzzerEncendido) "🔔 Sonando" else "🔕 Silencio",
                                style = MaterialTheme.typography.bodySmall,
                                color = if (buzzerEncendido) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    
                    Switch(
                        checked = buzzerEncendido,
                        onCheckedChange = { 
                            buzzerEncendido = it
                            viewModel.testBuzzer(it)
                        },
                        enabled = connectionState
                    )
                }
            }
        }
        
        // Acciones rápidas
        Text(
            text = "Acciones",
            style = MaterialTheme.typography.titleLarge
        )
        
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Button(
                    onClick = { viewModel.limpiarTodasLasDosisESP32() },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp),
                    enabled = connectionState,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Icon(
                        imageVector = Icons.Outlined.DeleteSweep,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Limpiar Todas las Dosis")
                }
            }
        }
        
        Spacer(modifier = Modifier.weight(1f))
        
        // Info de la app
        ElevatedCard(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = MaterialTheme.colorScheme.secondaryContainer
            )
        ) {
            Column(
                modifier = Modifier.padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Text(
                    text = "Dispensador de Medicamentos",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSecondaryContainer,
                    textAlign = TextAlign.Center
                )
                HorizontalDivider()
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Outlined.Info,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                        tint = MaterialTheme.colorScheme.onSecondaryContainer
                    )
                    Text(
                        text = "Versión 1.0",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSecondaryContainer
                    )
                }
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Outlined.DeveloperBoard,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                        tint = MaterialTheme.colorScheme.onSecondaryContainer
                    )
                    Text(
                        text = "ESP32 + MQTT",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSecondaryContainer
                    )
                }
            }
        }
    }
}
