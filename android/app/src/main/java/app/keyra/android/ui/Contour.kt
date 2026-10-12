package app.keyra.android.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.lerp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathMeasure
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.vector.PathParser

/** What the Contour shows (BRAND.md §5). */
enum class ContourState { IDLE, WAITING, DONE, FAILED }

// The mark on its 100-unit grid (BRAND.md §2). It starts and ends at the gap on the apex→corner edge.
private const val MARK = "M72.3 31.37 L75.5 33 V44 C75.5 60 64 73 50 80 C36 73 24.5 60 24.5 44 V33 L50 20 L63.8 27.04"

// Under 48 dp: stroke 8 and a wider gap (ends at x = 73.5 and 62.5 on the same edge).
private const val MARK_BOLD = "M73.5 31.98 L75.5 33 V44 C75.5 60 64 73 50 80 C36 73 24.5 60 24.5 44 V33 L50 20 L62.5 26.37"

private val EASE_OUT = CubicBezierEasing(0.2f, 0.8f, 0.2f, 1f)

/**
 * Keyra's mark, drawn as one line. As the press indicator: [ContourState.WAITING] is signal blue
 * with the gap breathing at the board LED's 1.2 s rhythm, [ContourState.DONE] closes the gap in
 * ink/ivory, [ContourState.FAILED] retracts the line to its two ends in the error colour. With
 * animations removed in Android's settings the states switch without motion.
 */
@Composable
fun Contour(modifier: Modifier = Modifier, state: ContourState = ContourState.IDLE, color: Color = MaterialTheme.colorScheme.onSurface, bold: Boolean = false) {
    val still = reducedMotion()
    val shape = remember(bold) {
        val path = PathParser().parsePathString(if (bold) MARK_BOLD else MARK).toPath()
        val measure = PathMeasure().apply { setPath(path, false) }
        val start = measure.getPosition(0f)
        val end = measure.getPosition(measure.length)
        Triple(measure, start, end)
    }
    val (measure, start, end) = shape
    val length = measure.length
    val segment = remember { Path() }

    val breath = if (state == ContourState.WAITING && !still) {
        rememberInfiniteTransition(label = "breath").animateFloat(
            initialValue = 0f,
            targetValue = 1f,
            animationSpec = infiniteRepeatable(tween(600, easing = EASE_OUT), RepeatMode.Reverse),
            label = "gap",
        ).value
    } else 0f
    val close by animateFloatAsState(
        if (state == ContourState.DONE) 1f else 0f,
        if (still) snap() else tween(420, easing = EASE_OUT),
        label = "close",
    )
    val retract by animateFloatAsState(
        if (state == ContourState.FAILED) 1f else 0f,
        if (still) snap() else tween(420, easing = EASE_OUT),
        label = "retract",
    )
    val target = when (state) {
        ContourState.IDLE -> color
        ContourState.WAITING -> signal
        ContourState.DONE -> MaterialTheme.colorScheme.onSurface
        ContourState.FAILED -> MaterialTheme.colorScheme.error
    }
    val tint by animateColorAsState(target, if (still) snap() else tween(320), label = "tint")
    val strokeUnits = if (bold) 8f else 5f

    Canvas(modifier) {
        val scale = size.minDimension / 100f
        translate((size.width - 100f * scale) / 2, (size.height - 100f * scale) / 2) {
            withTransform({ scale(scale, scale, pivot = Offset.Zero) }) {
                val stroke = Stroke(width = strokeUnits, cap = StrokeCap.Round, join = StrokeJoin.Round)
                fun draw(from: Float, to: Float) {
                    segment.reset()
                    if (measure.getSegment(from, to, segment, true)) drawPath(segment, tint, style = stroke)
                }
                when {
                    close > 0f -> {
                        draw(0f, length)
                        drawLine(tint, end, lerp(end, start, close), strokeWidth = strokeUnits, cap = StrokeCap.Round)
                    }
                    retract > 0f -> {
                        val keep = (length / 2 * (1 - retract)).coerceAtLeast(0.01f)
                        draw(0f, keep)
                        draw(length - keep, length)
                    }
                    else -> {
                        val gap = breath * 3f // grid units taken off each end at the widest breath
                        draw(gap, length - gap)
                    }
                }
            }
        }
    }
}
