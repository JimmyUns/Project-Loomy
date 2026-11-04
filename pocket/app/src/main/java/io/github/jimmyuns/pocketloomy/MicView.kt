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
// next to the mic and fading out (like the app icon and the dot strip on the PC). It shows what's going on
// without reading anything (his ask):
//   IDLE       dark mic, dots drifting slowly                       (not listening)
//   WAITING    dark mic, its ring breathing, dots a bit livelier     (listening for "Loomy")
//   LISTENING  purple mic, white ring, dots fast and swelling with the voice   (taking the command)
//   THINKING   mic dimmed, a ring spinning round it, dots still      (Ellune working it out / sending to the PC)
//   flash()    the ring goes green (done) or red (didn't work) for a moment, with a ripple
// Only the circle itself is the button. Animates only while it's on screen and not covered.
class MicView(ctx: Context, accent: Int, private val card: Int, private val bright: Int) : View(ctx) {
    /** the theme colour (purple, or blue in Bluetooth mode); the switch is animated by the screen */
    var accent = accent
        set(v) { field = v; invalidate() }
    enum class Mode { IDLE, WAITING, LISTENING, THINKING, DISABLED }

    var mode = Mode.IDLE
        set(v) { if (field != v) { field = v; invalidate() } }
    /** the mic level, -2..10 like Android's recognizer reports it */
    var level = 0f
    var onTap: () -> Unit = {}
    /** 2.22: holding the mic (HOLD_MS) mutes; a ring fills round it meanwhile. null = holding does nothing */
    var onHold: (() -> Unit)? = null
    private var holdStart = 0L
    private var held = false
    private val HOLD_MS = 800L
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
    private var spin = 0f            // the loading ring's angle
    private var flashColor = 0
    private var flashAt = 0L
    private val okC = Color.rgb(0x8E, 0xE3, 0xB8)
    private val errC = Color.rgb(0xFF, 0x7C, 0x96)

    /** a moment of green (it worked) or red (it didn't) */
    fun flash(good: Boolean) { flashColor = if (good) okC else errC; flashAt = SystemClock.uptimeMillis(); invalidate() }
    private var smooth = 0f          // the level, smoothed so the dots don't jitter
    private var lastT = 0L
    private var radius = 0f

    init {
        isClickable = true
        contentDescription = "Talk"
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val inside = hypot(e.x - width / 2f, e.y - height / 2f) <= radius * 1.15f
        when (e.action) {
            MotionEvent.ACTION_DOWN -> {
                if (!inside) return false
                held = false
                holdStart = if (onHold != null) SystemClock.uptimeMillis() else 0L
                invalidate()
                return true
            }
            MotionEvent.ACTION_UP -> {
                val wasHeld = held
                holdStart = 0L; held = false
                invalidate()
                if (!wasHeld && inside) { performClick(); onTap() }
            }
            MotionEvent.ACTION_CANCEL -> { holdStart = 0L; held = false; invalidate() }
        }
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
            Mode.IDLE -> 0.10
            Mode.WAITING -> 0.22
            Mode.LISTENING -> 0.45 + smooth * 1.0
            Mode.THINKING, Mode.DISABLED -> 0.0
        }
        phase = (phase + dt * speed) % 1.0
        spin = (spin + dt * 300f) % 360f
        val sinceFlash = now - flashAt
        val flashK = if (flashAt == 0L || sinceFlash > 1100) 0f else 1f - sinceFlash / 1100f

        val cx = width / 2f
        val cy = height / 2f
        radius = min(width, height) * 0.16f
        val step = radius * 0.42f                         // distance between rings
        val far = min(width, height) * 0.5f               // where the dots are gone
        val rings = ((far - radius) / step).toInt() + 2
        val maxDot = radius * 0.11f * (1f + smooth * 0.5f) * (if (mode == Mode.LISTENING) 1.15f else 1f)
        val dim = if (mode == Mode.DISABLED) 0.35f else 1f
        val dotsDim = when (mode) { Mode.THINKING -> 0.35f; Mode.IDLE -> 0.7f; else -> 1f } * dim
        dot.color = accent

        // the dots: rings moving outward, each a little smaller and fainter, staggered like a halftone
        for (k in 0 until rings) {
            val r = radius + step * 0.75f + step * (k + phase.toFloat())
            val t = ((r - radius) / (far - radius)).coerceIn(0f, 1f)
            if (t >= 1f) continue
            val size = maxDot * (1f - t).pow(1.3f)
            if (size < 0.6f) continue
            val fadeIn = ((r - radius - step * 0.5f) / step).coerceIn(0f, 1f)   // new rings appear softly
            dot.alpha = (255 * dotsDim * fadeIn * (1f - t).pow(0.8f)).toInt().coerceIn(0, 255)
            val n = max(6, (2 * PI * r / (step * 0.62f)).toInt())
            val off = if (k % 2 == 0) 0.0 else PI / n                          // halftone stagger
            for (i in 0 until n) {
                val a = off + 2 * PI * i / n
                c.drawCircle(cx + (r * cos(a)).toFloat(), cy + (r * sin(a)).toFloat(), size, dot)
            }
        }

        // the button
        val listening = mode == Mode.LISTENING
        val breathe = if (mode == Mode.WAITING) (0.5f + 0.5f * sin(now / 450.0).toFloat()) else 0f
        val swell = radius * (1f + smooth * 0.06f + flashK * 0.05f)
        fill.color = when (mode) {
            Mode.LISTENING -> accent
            Mode.THINKING -> blend(card, accent, 0.35f)
            else -> card
        }
        fill.alpha = (255 * dim).toInt()
        c.drawCircle(cx, cy, swell, fill)
        ring.color = when {
            flashK > 0f -> flashColor
            listening -> bright
            else -> accent
        }
        ring.strokeWidth = radius * (if (listening || flashK > 0f) 0.055f else 0.035f + breathe * 0.03f)
        ring.alpha = (255 * dim * (if (mode == Mode.WAITING) 0.55f + breathe * 0.45f else 1f)).toInt()
        c.drawCircle(cx, cy, swell, ring)
        if (flashK > 0f) {                                   // a ripple going out with the flash
            ring.alpha = (180 * flashK).toInt()
            ring.strokeWidth = radius * 0.03f
            c.drawCircle(cx, cy, swell * (1.1f + (1f - flashK) * 0.6f), ring)
        }
        if (mode == Mode.THINKING) {                         // the loading ring
            ring.color = bright
            ring.alpha = 255
            ring.strokeWidth = radius * 0.06f
            box.set(cx - swell * 1.18f, cy - swell * 1.18f, cx + swell * 1.18f, cy + swell * 1.18f)
            c.drawArc(box, spin, 90f, false, ring)
            ring.alpha = 90
            c.drawArc(box, spin + 180f, 60f, false, ring)
        }

        // the mic: a capsule, the cradle under it, the stand
        val u = radius * 0.1f
        glyph.strokeWidth = u * 0.95f
        glyph.color = if (listening) Color.WHITE else bright
        glyph.alpha = (255 * dim * (if (mode == Mode.THINKING) 0.45f else 1f)).toInt()
        box.set(cx - u * 1.9f, cy - u * 4.6f, cx + u * 1.9f, cy + u * 1.4f)
        c.drawRoundRect(box, u * 1.9f, u * 1.9f, glyph)
        path.reset()
        box.set(cx - u * 3.4f, cy - u * 3.2f, cx + u * 3.4f, cy + u * 3.0f)
        path.addArc(box, 15f, 150f)
        c.drawPath(path, glyph)
        c.drawLine(cx, cy + u * 3.0f, cx, cy + u * 4.6f, glyph)
        c.drawLine(cx - u * 1.8f, cy + u * 4.6f, cx + u * 1.8f, cy + u * 4.6f, glyph)

        // holding: a ring fills round the button; full = muted
        if (holdStart > 0L && !held) {
            val k = ((now - holdStart).toFloat() / HOLD_MS).coerceIn(0f, 1f)
            ring.color = bright
            ring.alpha = 255
            ring.strokeWidth = radius * 0.07f
            box.set(cx - swell * 1.22f, cy - swell * 1.22f, cx + swell * 1.22f, cy + swell * 1.22f)
            c.drawArc(box, -90f, 360f * k, false, ring)
            if (k >= 1f) { held = true; onHold?.invoke() }
        }

        if ((mode != Mode.DISABLED || holdStart > 0L) && running && isShown) postInvalidateOnAnimation()
    }

    private fun blend(a: Int, b: Int, k: Float) = Color.rgb(
        (Color.red(a) + (Color.red(b) - Color.red(a)) * k).toInt(),
        (Color.green(a) + (Color.green(b) - Color.green(a)) * k).toInt(),
        (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * k).toInt())

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

// The background (2.22, his ask): faint grid lines, like graph paper in the dark, fading out towards the edges
class GridDrawable(line: Int, private val bg: Int, private val cell: Float) : android.graphics.drawable.Drawable() {
    private val p = Paint().apply { color = line; strokeWidth = 1f }
    /** the lines' colour, alpha included (the theme and the Visuals slider change it) */
    fun setLine(c: Int) { p.color = c; invalidateSelf() }
    private val fade = Paint()
    override fun draw(c: Canvas) {
        val b = bounds
        c.drawColor(bg)
        var x = b.left + (b.width() % cell) / 2f
        while (x <= b.right) { c.drawLine(x, b.top.toFloat(), x, b.bottom.toFloat(), p); x += cell }
        var y = b.top + (b.height() % cell) / 2f
        while (y <= b.bottom) { c.drawLine(b.left.toFloat(), y, b.right.toFloat(), y, p); y += cell }
        val r = max(b.width(), b.height()) * 0.72f
        fade.shader = android.graphics.RadialGradient(b.exactCenterX(), b.exactCenterY() * 0.9f, r,
            intArrayOf(Color.TRANSPARENT, Color.argb(120, Color.red(bg), Color.green(bg), Color.blue(bg)), bg),
            floatArrayOf(0f, 0.6f, 1f), android.graphics.Shader.TileMode.CLAMP)
        c.drawRect(b, fade)
    }
    override fun setAlpha(alpha: Int) {}
    override fun setColorFilter(cf: android.graphics.ColorFilter?) {}
    @Deprecated("Deprecated in Java")
    override fun getOpacity() = android.graphics.PixelFormat.OPAQUE
}

// The Bluetooth button over the mic (2.22): the Bluetooth rune, plain when Ellune mode is on, filled when
// Bluetooth mode is (then everything goes over Bluetooth and Ellune is left alone)
class BtIconView(ctx: Context, private val plain: Int) : View(ctx) {
    var active = false
        set(v) { field = v; invalidate() }
    var accent = plain
        set(v) { field = v; invalidate() }
    private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeJoin = Paint.Join.ROUND; strokeCap = Paint.Cap.ROUND }
    private val f = Paint(Paint.ANTI_ALIAS_FLAG)
    private val path = Path()

    init { isClickable = true; contentDescription = "Bluetooth mode" }

    override fun onDraw(c: Canvas) {
        val cx = width / 2f
        val cy = height / 2f
        val r = min(width, height) * 0.46f
        f.color = if (active) accent else Color.argb(0, 0, 0, 0)
        if (active) c.drawCircle(cx, cy, r, f)
        p.color = if (active) Color.WHITE else accent
        p.strokeWidth = r * 0.11f
        if (!active) { p.alpha = 150; c.drawCircle(cx, cy, r - p.strokeWidth, p); p.alpha = 255 }
        val h = r * 0.55f
        val w = r * 0.3f
        path.reset()                                     // the rune: up the spine, two arrows back across it
        path.moveTo(cx - w, cy - h * 0.5f)
        path.lineTo(cx + w, cy + h * 0.5f)
        path.lineTo(cx, cy + h)
        path.lineTo(cx, cy - h)
        path.lineTo(cx + w, cy - h * 0.5f)
        path.lineTo(cx - w, cy + h * 0.5f)
        c.drawPath(path, p)
    }
}

// 2.22: the power symbol. Big and beating in the middle while Pocket Loomy is off (tap: on); small at the top left
// while it's on (tap: off).
class PowerView(ctx: Context, accent: Int, private val card: Int, private val big: Boolean) : View(ctx) {
    var accent = accent
        set(v) { field = v; invalidate() }
    private val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND }
    private val f = Paint(Paint.ANTI_ALIAS_FLAG)
    private val box = RectF()

    init { isClickable = true; contentDescription = if (big) "Turn on" else "Turn off" }

    override fun onDraw(c: Canvas) {
        val now = SystemClock.uptimeMillis()
        val cx = width / 2f
        val cy = height / 2f
        var r = min(width, height) * (if (big) 0.16f else 0.4f)
        if (big) {
            val beat = (0.5f + 0.5f * sin(now / 380.0).toFloat())           // a slow heartbeat
            f.color = accent
            for (i in 3 downTo 1) {                                          // a soft glow that breathes
                f.alpha = (26 * beat / i).toInt()
                c.drawCircle(cx, cy, r * (1.2f + 0.25f * i + 0.08f * beat), f)
            }
            r *= 1f + 0.05f * beat
            f.alpha = 255; f.color = card
            c.drawCircle(cx, cy, r, f)
            p.color = accent; p.strokeWidth = r * 0.05f
            c.drawCircle(cx, cy, r, p)
        }
        p.color = accent
        p.strokeWidth = r * (if (big) 0.1f else 0.14f)
        val g = r * (if (big) 0.42f else 0.62f)
        box.set(cx - g, cy - g, cx + g, cy + g)
        c.drawArc(box, -60f, 300f, false, p)                                   // the open circle
        c.drawLine(cx, cy - g * 1.15f, cx, cy - g * 0.15f, p)                  // the line through the gap
        if (big && isShown) postInvalidateOnAnimation()
    }
}
