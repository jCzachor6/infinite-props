package dev.infiniteprops

import android.graphics.Bitmap
import java.nio.ByteBuffer

/** Exact top-down RGBA storage, including RGB under zero alpha. Suitable for
 * lossless export and texture upload. Android Canvas requires premultiplied
 * data: use toPremultipliedBitmap() when drawing through the view system. */
fun GeneratedImage.toBitmap(): Bitmap {
    val bitmap = Bitmap.createBitmap(width.toInt(), height.toInt(), Bitmap.Config.ARGB_8888)
    try {
        bitmap.setPremultiplied(false)
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgba))
        return bitmap
    } catch (error: Throwable) { bitmap.recycle(); throw error }
}

/** Explicit Android presentation conversion. Premultiplication is lossy for
 * translucent pixels; it must never be used as the generator's RGBA oracle. */
fun GeneratedImage.toPremultipliedBitmap(): Bitmap {
    val colors = IntArray((width.toLong() * height.toLong()).toInt())
    for (i in colors.indices) {
        val at = i * 4
        colors[i] = ((rgba[at + 3].toInt() and 255) shl 24) or
            ((rgba[at].toInt() and 255) shl 16) or
            ((rgba[at + 1].toInt() and 255) shl 8) or (rgba[at + 2].toInt() and 255)
    }
    return Bitmap.createBitmap(colors, width.toInt(), height.toInt(), Bitmap.Config.ARGB_8888)
}
