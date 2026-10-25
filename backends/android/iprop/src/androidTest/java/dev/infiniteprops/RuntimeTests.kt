package dev.infiniteprops

import androidx.test.platform.app.InstrumentationRegistry
import dev.infiniteprops.tests.verifyBindings
import dev.infiniteprops.tests.verifyPixelCorpus
import java.nio.ByteBuffer
import org.junit.Assert.assertArrayEquals
import org.junit.Test
import org.junit.Assert.assertEquals

class RuntimeTests {
    @Test fun nativeContracts() {
        val assets = InstrumentationRegistry.getInstrumentation().context.assets
        verifyBindings { assets.open(it).use { stream -> stream.readBytes() } }
        verifyPixelCorpus(load = { assets.open(it).use { stream -> stream.readBytes() } }) { image, expected, index ->
            val bitmap = image.toBitmap()
            try {
                assertEquals(image.width.toInt(), bitmap.width)
                assertEquals(image.height.toInt(), bitmap.height)
                val actual = ByteBuffer.allocate(bitmap.byteCount)
                bitmap.copyPixelsToBuffer(actual)
                assertArrayEquals("Bitmap RGBA case $index", expected, actual.array())
            } finally { bitmap.recycle() }
        }
        Program(assets.open("bindings.iprop").use { it.readBytes() }).use { program ->
            val result = program.generate(inputs = mapOf("tint" to InputValue.RGBA(Color(18, 52, 86))))
            val bitmap = result.toBitmap()
            try {
                assertEquals(8, bitmap.width)
                assertEquals(4, bitmap.height)
                assertEquals(0xff123456.toInt(), bitmap.getPixel(0, 0))
            } finally { bitmap.recycle() }
        }
    }
}
