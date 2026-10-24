package dev.infiniteprops

import androidx.test.platform.app.InstrumentationRegistry
import dev.infiniteprops.tests.verifyBindings
import org.junit.Test
import org.junit.Assert.assertEquals

class RuntimeTests {
    @Test fun nativeContracts() {
        val assets = InstrumentationRegistry.getInstrumentation().context.assets
        verifyBindings { assets.open(it).use { stream -> stream.readBytes() } }
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
