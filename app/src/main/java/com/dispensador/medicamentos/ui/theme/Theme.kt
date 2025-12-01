package com.dispensador.medicamentos.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

private val DarkColorScheme = darkColorScheme(
    primary = TurquoiseMain,
    onPrimary = Color.White,
    primaryContainer = TurquoiseDark,
    onPrimaryContainer = MintBackground,
    
    secondary = TurquoiseLight,
    onSecondary = Grey90,
    secondaryContainer = MintContainer,
    onSecondaryContainer = Grey90,
    
    tertiary = SuccessGreen,
    
    background = Grey90,
    onBackground = Color.White,
    
    surface = Grey80,
    onSurface = Color.White,
    
    error = ErrorRed,
    onError = Color.White
)

private val LightColorScheme = lightColorScheme(
    primary = TurquoiseMain,
    onPrimary = Color.White,
    primaryContainer = MintContainer,
    onPrimaryContainer = TurquoiseDark,
    
    secondary = TurquoiseDark,
    onSecondary = Color.White,
    secondaryContainer = MintBackground,
    onSecondaryContainer = TurquoiseDark,
    
    tertiary = SuccessGreen,
    
    background = Color.White,
    onBackground = Grey90,
    
    surface = Color.White,
    onSurface = Grey90,
    
    surfaceVariant = MintBackground,
    onSurfaceVariant = Grey80,
    
    error = ErrorRed,
    onError = Color.White,
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002)
)

@Composable
fun HealthPillTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    // Deshabilitamos dynamic color para usar nuestros colores personalizados
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val context = LocalContext.current
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }

        darkTheme -> DarkColorScheme
        else -> LightColorScheme
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}
