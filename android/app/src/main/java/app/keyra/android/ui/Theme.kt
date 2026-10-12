package app.keyra.android.ui

import android.provider.Settings
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

// Graphite & Ivory (docs/brand/BRAND.md §3). Monochrome on purpose: the one colour, signal blue,
// means only "Keyra is waiting for your press", so it is not in the Material scheme at all.
private val Ink = Color(0xFF111214)
private val Ivory = Color(0xFFF2F2EF)

private val Light = lightColorScheme(
    primary = Ink,
    onPrimary = Color(0xFFF4F3EF),
    primaryContainer = Color(0xFFE9E8E3),
    onPrimaryContainer = Ink,
    inversePrimary = Ivory,
    secondary = Color(0xFF4A4C52),
    onSecondary = Color.White,
    secondaryContainer = Color(0xFFE9E8E3), // fill: neutral controls (tonal buttons)
    onSecondaryContainer = Ink,
    tertiary = Color(0xFF4A4C52),
    onTertiary = Color.White,
    tertiaryContainer = Color(0xFFE9E8E3),
    onTertiaryContainer = Ink,
    background = Color(0xFFF4F3EF),
    onBackground = Ink,
    surface = Color(0xFFF4F3EF), // top bars sit on the page
    onSurface = Ink,
    surfaceVariant = Color(0xFFEFEEE9),
    onSurfaceVariant = Color(0xFF4A4C52),
    surfaceTint = Color.Transparent,
    inverseSurface = Ink,
    inverseOnSurface = Color(0xFFF4F3EF),
    error = Color(0xFFC1251C),
    onError = Color.White,
    errorContainer = Color(0xFFF7E4E2),
    onErrorContainer = Color(0xFF8A1A14),
    outline = Color(0xFF8A8C93), // 3 : 1 on the page, for field borders
    outlineVariant = Color(0x1A111214), // hairline
    scrim = Color.Black,
    surfaceBright = Color.White,
    surfaceDim = Color(0xFFE9E8E3),
    surfaceContainerLowest = Color.White, // rows and cards
    surfaceContainerLow = Color.White,
    surfaceContainer = Color(0xFFEFEEE9), // inputs, grouped background
    surfaceContainerHigh = Color.White, // dialogs
    surfaceContainerHighest = Color(0xFFE9E8E3),
)

private val Dark = darkColorScheme(
    primary = Ivory,
    onPrimary = Color(0xFF0B0C0E),
    primaryContainer = Color(0xFF222328),
    onPrimaryContainer = Ivory,
    inversePrimary = Ink,
    secondary = Color(0xFFA9ABB2),
    onSecondary = Color(0xFF0B0C0E),
    secondaryContainer = Color(0xFF222328),
    onSecondaryContainer = Ivory,
    tertiary = Color(0xFFA9ABB2),
    onTertiary = Color(0xFF0B0C0E),
    tertiaryContainer = Color(0xFF222328),
    onTertiaryContainer = Ivory,
    background = Color(0xFF0B0C0E),
    onBackground = Ivory,
    surface = Color(0xFF0B0C0E),
    onSurface = Ivory,
    surfaceVariant = Color(0xFF1B1C20),
    onSurfaceVariant = Color(0xFFA9ABB2),
    surfaceTint = Color.Transparent,
    inverseSurface = Ivory,
    inverseOnSurface = Color(0xFF0B0C0E),
    error = Color(0xFFFF7A70),
    onError = Color(0xFF0B0C0E),
    errorContainer = Color(0xFF3A1614),
    onErrorContainer = Color(0xFFFFB4AD),
    outline = Color(0xFF64666D),
    outlineVariant = Color(0x14FFFFFF),
    scrim = Color.Black,
    surfaceBright = Color(0xFF222328),
    surfaceDim = Color(0xFF0B0C0E),
    surfaceContainerLowest = Color(0xFF141518),
    surfaceContainerLow = Color(0xFF141518),
    surfaceContainer = Color(0xFF1B1C20),
    surfaceContainerHigh = Color(0xFF1B1C20),
    surfaceContainerHighest = Color(0xFF222328),
)

/** Waiting for the press. Only that. */
val signal: Color
    @Composable @ReadOnlyComposable
    get() = if (isSystemInDarkTheme()) Color(0xFF6C9CFF) else Color(0xFF1F5BEF)

val okColor: Color
    @Composable @ReadOnlyComposable
    get() = if (isSystemInDarkTheme()) Color(0xFF4ADE80) else Color(0xFF0A7A3D)

private val KeyraShapes = Shapes(
    extraSmall = RoundedCornerShape(8.dp),
    small = RoundedCornerShape(12.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(20.dp),
    extraLarge = RoundedCornerShape(28.dp),
)

/** Headings 600, body 400; tight display tracking in Latin only, Arabic 6 % larger (BRAND §4). */
private fun typography(arabic: Boolean): Typography {
    val base = Typography()
    fun TextStyle.sized(): TextStyle = if (arabic) copy(fontSize = fontSize * 1.06f, lineHeight = lineHeight * 1.12f) else this
    fun TextStyle.heading(): TextStyle = copy(fontWeight = FontWeight.SemiBold, letterSpacing = if (arabic) 0.sp else (-0.02).em).sized()
    fun TextStyle.weight(w: FontWeight): TextStyle = copy(fontWeight = w).sized()
    return Typography(
        displayLarge = base.displayLarge.heading(),
        displayMedium = base.displayMedium.heading(),
        displaySmall = base.displaySmall.heading(),
        headlineLarge = base.headlineLarge.heading(),
        headlineMedium = base.headlineMedium.heading(),
        headlineSmall = base.headlineSmall.heading(),
        titleLarge = base.titleLarge.heading(),
        titleMedium = base.titleMedium.weight(FontWeight.SemiBold),
        titleSmall = base.titleSmall.weight(FontWeight.Medium),
        bodyLarge = base.bodyLarge.weight(FontWeight.Normal),
        bodyMedium = base.bodyMedium.weight(FontWeight.Normal),
        bodySmall = base.bodySmall.weight(FontWeight.Normal),
        labelLarge = base.labelLarge.weight(FontWeight.Medium),
        labelMedium = base.labelMedium.weight(FontWeight.Medium),
        labelSmall = base.labelSmall.weight(FontWeight.Medium),
    )
}

@Composable
fun KeyraTheme(content: @Composable () -> Unit) {
    val arabic = LocalLayoutDirection.current == LayoutDirection.Rtl
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) Dark else Light,
        shapes = KeyraShapes,
        typography = remember(arabic) { typography(arabic) },
        content = content,
    )
}

/** Android's "Remove animations" (animator duration scale 0): no drawing or breathing. */
@Composable
fun reducedMotion(): Boolean {
    val resolver = LocalContext.current.contentResolver
    return remember(resolver) { Settings.Global.getFloat(resolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f }
}

/** A small caption above a group of rows. */
@Composable
fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = modifier.padding(start = 4.dp, end = 4.dp, bottom = 8.dp),
    )
}

/** Rows on one raised surface, separated by hairlines ([RowDivider]). */
@Composable
fun Group(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier.fillMaxWidth().clip(MaterialTheme.shapes.large).background(MaterialTheme.colorScheme.surfaceContainerLowest),
        content = content,
    )
}

/** The hairline between two rows of a [Group], inset to where the row's text starts. */
@Composable
fun RowDivider(inset: Dp = 16.dp) {
    HorizontalDivider(Modifier.padding(start = inset), thickness = 1.dp, color = MaterialTheme.colorScheme.outlineVariant)
}

/** The shape of row [index] of [count] in a group that a lazy list draws item by item. */
fun groupShape(index: Int, count: Int, radius: Dp = 20.dp): Shape {
    val top = if (index == 0) radius else 0.dp
    val bottom = if (index == count - 1) radius else 0.dp
    return RoundedCornerShape(topStart = top, topEnd = top, bottomStart = bottom, bottomEnd = bottom)
}
