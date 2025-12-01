package com.dispensador.medicamentos.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.dispensador.medicamentos.ui.screens.AjustesScreen
import com.dispensador.medicamentos.ui.screens.ConfigurarDosisScreen
import com.dispensador.medicamentos.ui.screens.DashboardScreen
import com.dispensador.medicamentos.ui.screens.HistorialScreen
import com.dispensador.medicamentos.ui.viewmodel.DispensadorViewModel

sealed class Screen(val route: String, val title: String, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    object Dashboard : Screen("dashboard", "Inicio", Icons.Default.Home)
    object Config : Screen("config", "Configurar", Icons.Default.AddCircle)
    object History : Screen("history", "Historial", Icons.Default.History)
    object Settings : Screen("settings", "Ajustes", Icons.Default.Settings)
}

@Composable
fun DispensadorApp(
    viewModel: DispensadorViewModel,
    modifier: Modifier = Modifier
) {
    val navController = rememberNavController()
    val screens = listOf(
        Screen.Dashboard,
        Screen.Config,
        Screen.History,
        Screen.Settings
    )
    
    Scaffold(
        bottomBar = {
            NavigationBar {
                val navBackStackEntry by navController.currentBackStackEntryAsState()
                val currentRoute = navBackStackEntry?.destination?.route
                
                screens.forEach { screen ->
                    NavigationBarItem(
                        icon = { Icon(screen.icon, contentDescription = screen.title) },
                        label = { Text(screen.title) },
                        selected = currentRoute == screen.route,
                        onClick = {
                            navController.navigate(screen.route) {
                                popUpTo(navController.graph.startDestinationId) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        }
                    )
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Dashboard.route,
            modifier = Modifier.padding(innerPadding)
        ) {
            composable(Screen.Dashboard.route) {
                DashboardScreen(viewModel = viewModel)
            }
            composable(Screen.Config.route) {
                ConfigurarDosisScreen(viewModel = viewModel)
            }
            composable(Screen.History.route) {
                HistorialScreen(viewModel = viewModel)
            }
            composable(Screen.Settings.route) {
                AjustesScreen(viewModel = viewModel)
            }
        }
    }
}
