package dev.infiniteprops

data class Color(val r: Int, val g: Int, val b: Int, val a: Int = 255) {
    init { require(listOf(r, g, b, a).all { it in 0..255 }) }
    internal val bits: Int get() = (r shl 24) or (g shl 16) or (b shl 8) or a
    companion object { internal fun fromBits(v: Int) = Color(v ushr 24, (v ushr 16) and 255, (v ushr 8) and 255, v and 255) }
}

sealed class InputValue {
    data class F32(val value: Float) : InputValue()
    data class U32(val value: UInt) : InputValue()
    data class I32(val value: Int) : InputValue()
    data class RGBA(val value: Color) : InputValue()
}
data class Input(val index: UInt, val name: String, val defaultValue: InputValue)
data class Metadata(val width: UInt, val height: UInt, val originX: Float, val originY: Float,
    val fuel: ULong, val emissionColor: Color?, val emissionOffsetX: Float,
    val emissionOffsetY: Float, val emissionRadius: Float, val containerFlags: UInt)
enum class OutputKind { POINT, RECT, CIRCLE }
data class Output(val index: UInt, val owner: UInt, val name: String, val kind: OutputKind,
    val isList: Boolean, val count: UInt, val values: FloatArray)
data class Statistics(val fuelBudget: ULong, val fuelRemaining: ULong, val fuelUsed: ULong,
    val imageByteCount: ULong, val outputElementCount: ULong)
data class GeneratedImage(val width: UInt, val height: UInt, val rowStride: ULong,
    val pixelFormat: UInt, val alphaMode: UInt, val storageOrigin: UInt,
    val rgba: ByteArray, val outputs: List<Output>, val statistics: Statistics) {
    fun output(name: String, owner: UInt = 0u): Output? = outputs.firstOrNull { it.owner == owner && it.name == name }
}

class IpropException(message: String, val status: Int, val reason: Int, val stage: Int,
    val byteOffset: Long, val locationFlags: Int, val offsetSpace: Int,
    val functionIndex: Int, val pc: Int, val debugLine: Int, val messageTruncated: Boolean) : RuntimeException(message)

/** Native calls and close serialize on this instance. Use independent programs
 * for parallel generation. Every result owns copies, including exact f32 bits. */
class Program(bytes: ByteArray) : AutoCloseable {
    private var handle = Native.load(bytes)
    val metadata: Metadata
    val inputs: List<Input>
    init {
        try {
            val m = Native.metadata(handle)
            metadata = Metadata(m[0].toUInt(), m[1].toUInt(), Float.fromBits(m[2].toInt()), Float.fromBits(m[3].toInt()),
                m[4].toULong(), if (m[5] == 0L) null else Color.fromBits(m[6].toInt()),
                Float.fromBits(m[7].toInt()), Float.fromBits(m[8].toInt()), Float.fromBits(m[9].toInt()), m[10].toUInt())
            inputs = (0 until Native.inputCount(handle)).map { index ->
                val info = Native.inputInfo(handle, index)
                val value = when (info[0].toInt()) {
                    0 -> InputValue.RGBA(Color.fromBits(info[1].toInt()))
                    1 -> InputValue.F32(Float.fromBits(info[1].toInt()))
                    else -> InputValue.U32(info[1].toUInt())
                }
                Input(index.toUInt(), Native.inputName(handle, index).toString(Charsets.UTF_8), value)
            }
        } catch (failure: Throwable) { close(); throw failure }
    }

    @Synchronized
    fun generate(seed: UInt = 0u, inputs: Map<String, InputValue> = emptyMap(), width: UInt = 0u, height: UInt = 0u): GeneratedImage {
        check(handle != 0L) { "Program is closed" }
        Native.begin(handle)
        try {
            for ((name, value) in inputs) {
                val (type, bits) = when (value) {
                    is InputValue.RGBA -> 0 to value.value.bits
                    is InputValue.F32 -> 1 to value.value.toRawBits()
                    is InputValue.U32 -> 2 to value.value.toInt()
                    is InputValue.I32 -> 2 to value.value
                }
                Native.set(handle, name.toByteArray(Charsets.UTF_8), type, bits)
            }
            val result = Native.generate(handle, seed.toInt(), width.toInt(), height.toInt())
            try {
                val info = Native.imageInfo(result)
                val stats = Native.statistics(result)
                val outputs = (0 until Native.outputCount(result)).map { index ->
                    val slot = Native.outputInfo(result, index)
                    Output(index.toUInt(), slot[0].toUInt(), Native.outputName(result, index).toString(Charsets.UTF_8),
                        OutputKind.entries[slot[1].toInt()], slot[2] != 0L, slot[3].toUInt(),
                        Native.outputValues(result, index).map { Float.fromBits(it) }.toFloatArray())
                }
                return GeneratedImage(info[0].toUInt(), info[1].toUInt(), info[2].toULong(),
                    info[3].toUInt(), info[4].toUInt(), info[5].toUInt(), Native.pixels(result), outputs,
                    Statistics(stats[0].toULong(), stats[1].toULong(), stats[2].toULong(), stats[3].toULong(), stats[4].toULong()))
            } finally { Native.freeResult(result) }
        } finally { Native.end(handle) }
    }

    @Synchronized
    override fun close() { if (handle != 0L) { Native.close(handle); handle = 0L } }

    @Suppress("deprecation")
    protected fun finalize() { close() }
}

internal object Native {
    init { System.loadLibrary("iprop_jni") }
    external fun load(bytes: ByteArray): Long
    external fun close(program: Long)
    external fun metadata(program: Long): LongArray
    external fun inputCount(program: Long): Int
    external fun inputInfo(program: Long, index: Int): LongArray
    external fun inputName(program: Long, index: Int): ByteArray
    external fun begin(program: Long)
    external fun end(program: Long)
    external fun set(program: Long, name: ByteArray, type: Int, bits: Int)
    external fun generate(program: Long, seed: Int, width: Int, height: Int): Long
    external fun freeResult(result: Long)
    external fun imageInfo(result: Long): LongArray
    external fun pixels(result: Long): ByteArray
    external fun statistics(result: Long): LongArray
    external fun outputCount(result: Long): Int
    external fun outputInfo(result: Long, index: Int): LongArray
    external fun outputName(result: Long, index: Int): ByteArray
    external fun outputValues(result: Long, index: Int): IntArray
}
