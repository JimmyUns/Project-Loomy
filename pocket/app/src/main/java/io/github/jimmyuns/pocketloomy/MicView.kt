package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin

// The middle of the app (2.22): a round mic button with Loomy's halftone dots rippling out from it, biggest
// next to the mic and fading out (like the app icon and the dot strip on the PC). Calm and slow while idle,
// faster and swelling with your voice while listening, a quick pulse while talking to the PC. Only the
// circle itself is the button. Animates only while it's on screen.
class MicView(ctx: Context, private val accent: Int, private val card: Int, private val bright: Int) : View(ctx) {
    enum class Mode { IDLE, LISTENING, SENDING, DISABLED }

    var mode = Mode.IDLE
        set(v) { if (field != v) { field = v; invalidate() } }
    /** the mic level, -2..10 like Android's recognizer reports it */
    var level = 0f
    var onTap: () -> Unit = {}
    /** false while something covers it (settings, history): no animating behind them */
    var running = true
        set(v) { if (field != v) { field = v; lastT = 0L; invalidate() } }

    private val dot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = accent }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val glyph = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; color = bright }
    private val path = Path()
    private val box = RectF()

    private var phase = 0.0          // 0..1: how far the rings have moved out by one step
    private var smooth = 0f          // the level, smoothed so the dots don't jitter
    private var lastT = 0L
    private var radius = 0f

    init {
        isClickable = true
        contentDescription = "Talk"
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val inside = hypot(e.x - width / 2f, e.y - height / 2f) <= radius * 1.15f
        if (e.action == MotionEvent.ACTION_DOWN) return inside
        if (e.action == MotionEvent.ACTION_UP && inside) { performClick(); onTap() }
        return true
    }

    override fun performClick(): Boolean { super.performClick(); return true }

    override fun onDraw(c: Canvas) {
        val now = SystemClock.uptimeMillis()
        val dt = if (lastT == 0L) 0f else min((now - lastT) / 1000f, 0.1f)
        lastT = now
        val lv = if (mode == Mode.LISTENING) (level.coerceIn(0f, 10f) / 10f) else 0f
        smooth += (lv - smooth) * min(1f, dt * 8f)
        val speed = when (mode) {
            Mode.IDLE -> 0.12
            Mode.LISTENING -> 0.35 + smooth * 0.9
            Mode.SENDING -> 0.9
            Mode.DISABLED -> 0.0
        }
        phase = (phase + dt * speed) % 1.0

        val cx = width / 2f
        val cy = height / 2f
        radius = min(width, height) * 0.16f
        val step = radius * 0.42f                         // distance between rings
        val far = min(width, height) * 0.5f               // where the dots are gone
        val rings = ((far - radius) / step).toInt() + 2
        val maxDot = radius * 0.11f * (1f + smooth * 0.5f)
        val dim = if (mode == Mode.DISABLED) 0.35f else 1f

        // the dots: rings moving outward, each a little smaller and fainter, staggered like a halftone
        for (k in 0 until rings) {
            val r = radius + step * 0.75f + step * (k + phase.toFloat())
            val t = ((r - radius) / (far - radius)).coerceIn(0f, 1f)
            if (t >= 1f) continue
            val size = maxDot * (1f - t).pow(1.3f)
            if (size < 0.6f) continue
            val fadeIn = ((r - radius - step * 0.5f) / step).coerceIn(0f, 1f)   // new rings appear softly
            dot.alpha = (255 * dim * fadeIn * (1f - t).pow(0.8f)).toInt().coerceIn(0, 255)
            val n = max(6, (2 * PI * r / (step * 0.62f)).toInt())
            val off = if (k % 2 == 0) 0.0 else PI / n                          // halftone stagger
            for (i in 0 until n) {
                val a = off + 2 * PI * i / n
                c.drawCircle(cx + (r * cos(a)).toFloat(), cy + (r * sin(a)).toFloat(), size, dot)
            }
        }

        // the button
        val listening = mode == Mode.LISTENING
        val swell = radius * (1f + smooth * 0.06f)
        fill.color = when (mode) {
            Mode.LISTENING, Mode.SENDING -> accent
            else -> card
        }
        fill.alpha = (255 * dim).toInt()
        c.drawCircle(cx, cy, swell, fill)
        ring.color = if (listening) bright else accent
        ring.strokeWidth = radius * (if (listening) 0.05f else 0.035f)
        ring.alpha = (255 * dim).toInt()
        c.drawCircle(cx, cy, swell, ring)

        // the mic: a capsule, the cradle under it, the stand
        val u = radius * 0.1f
        glyph.strokeWidth = u * 0.95f
        glyph.color = if (listening || mode == Mode.SENDING) Color.WHITE else bright
        glyph.alpha = (255 * dim).toInt()
        box.set(cx - u * 1.9f, cy - u * 4.6f, cx + u * 1.9f, cy + u * 1.4f)
        c.drawRoundRect(box, u * 1.9f, u * 1.9f, glyph)
        path.reset()
        box.set(cx - u * 3.4f, cy - u * 3.2f, cx + u * 3.4f, cy + u * 3.0f)
        path.addArc(box, 15f, 150f)
        c.drawPath(path, glyph)
        c.drawLine(cx, cy + u * 3.0f, cx, cy + u * 4.6f, glyph)
        c.drawLine(cx - u * 1.8f, cy + u * 4.6f, cx + u * 1.8f, cy + u * 4.6f, glyph)

        if (mode != Mode.DISABLED && running && isShown) postInvalidateOnAnimation()
    }

    override fun onVisibilityChanged(v: View, visibility: Int) {
        super.onVisibilityChanged(v, visibility)
        if (visibility == VISIBLE) { lastT = 0L; invalidate() }
    }
}

// The settings button: a small gear, drawn (no icon files needed)
class GearView(ctx: Context, private val color: Int) : View(ctx) {
    private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeJoin = Paint.Join.ROUND }
    private val path = Path()

    init { isClickable = true; contentDescription = "Settings" }

    override fun onDraw(c: Canvas) {
        val cx = width / 2f
        val cy = height / 2f
        val r = min(width, height) * 0.3f
        p.color = color
        p.strokeWidth = r * 0.2f
        path.reset()
        val teeth = 8
        for (i in 0 until teeth * 4) {                   // out, out, in, in: square-ish teeth
            val a = 2 * PI * i / (teeth * 4) - PI / (teeth * 4)
            val rr = if ((i / 2) % 2 == 0) r * 1.25f else r * 0.98f
            val x = cx + (rr * cos(a)).toFloat()
            val y = cy + (rr * sin(a)).toFloat()
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        path.close()
        c.drawPath(path, p)
        c.drawCircle(cx, cy, r * 0.42f, p)
    }
}
