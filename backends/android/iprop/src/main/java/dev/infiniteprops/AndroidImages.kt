package dev.infiniteprops

import android.graphics.Bitmap

/** Presentation interprets stored channels as straight RGBA; Bitmap performs
 * its own premultiplication. The raw rgba bytes and alphaMode stay unchanged. */
fun GeneratedImage.toBitmap(): Bitmap {
    val colors = IntArray((width.toLong() * height.toLong()).toInt())
    for (i in colors.indices) {
        val at = i * 4
        colors[i] = ((rgba[at + 3].toInt() and 255) shl 24) or
            ((rgba[at].toInt() and 255) shl 16) or
            ((rgba[at + 1].toInt() and 255) shl 8) or (rgba[at + 2].toInt() and 255)
    }
    return Bitmap.createBitmap(colors, width.toInt(), height.toInt(), Bitmap.Config.ARGB_8888)
}
