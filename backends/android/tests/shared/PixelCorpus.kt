package dev.infiniteprops.tests

import dev.infiniteprops.*
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

fun verifyPixelCorpus(load: (String) -> ByteArray, transport: (GeneratedImage, ByteArray, Int) -> Unit = { _, _, _ -> }) {
    val data = load("pixel-corpus.bin")
    val digest = MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }
    check(load("pixel-corpus.json").toString(Charsets.UTF_8).contains(digest)) { "Corpus SHA-256 mismatch" }
    val reader = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
    fun word() = reader.int
    fun blob(): ByteArray { val count = word(); check(count >= 0 && count <= reader.remaining()); return ByteArray(count).also { reader.get(it) } }
    check(ByteArray(8).also { reader.get(it) }.toString(Charsets.US_ASCII) == "IPXCRP01")
    val programs = List(word()) { blob() }
    val count = word(); check(count > 0)
    repeat(count) { index ->
        Program(programs[word()]).use { program ->
            val seed = word().toUInt(); val width = word().toUInt(); val height = word().toUInt()
            val inputs = mutableMapOf<String, InputValue>()
            repeat(word()) {
                val name = blob().toString(Charsets.UTF_8)
                val tag = word(); val bits = word(); val integer = word()
                inputs[name] = if (tag == 0) InputValue.RGBA(Color(bits and 255, (bits ushr 8) and 255, (bits ushr 16) and 255, bits ushr 24))
                    else if (program.inputs.first { it.name == name }.defaultValue is InputValue.F32) InputValue.F32(Float.fromBits(bits))
                    else InputValue.U32(integer.toUInt())
            }
            val expectedWidth = word().toUInt(); val expectedHeight = word().toUInt(); val expected = blob()
            val image = program.generate(seed, inputs, width, height)
            val label = "Corpus case $index seed $seed"
            check(image.width == expectedWidth && image.height == expectedHeight) { "$label: size" }
            check(image.rgba.contentEquals(expected)) { "$label: RGBA" }
            val outputs = image.outputs
            check(outputs.size == word()) { "$label: outputs" }
            for (output in outputs) {
                check(output.name.toByteArray(Charsets.UTF_8).contentEquals(blob())) { "$label: name" }
                word() // Reserved; WASM does not expose owner IDs.
                val kind = word(); val list = word() != 0; val elements = word().toUInt()
                check(output.kind.ordinal == kind && output.isList == list && output.count == elements) { "$label: slot" }
                check(output.values.size == word()) { "$label: values" }
                output.values.forEach { check(it.toRawBits() == word()) { "$label: f32 bits" } }
            }
            transport(image, expected, index)
        }
    }
    check(!reader.hasRemaining()) { "Unread corpus cases" }
    println("Android/JNI: $count pixel-exact cases; SHA-256 $digest")
}
