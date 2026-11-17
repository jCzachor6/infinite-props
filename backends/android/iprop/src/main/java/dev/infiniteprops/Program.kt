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
enum class OutputKind { POINT, RECT, CIRCLE, PART }
data class Output @JvmOverloads constructor(val index: UInt, val owner: UInt, val name: String, val kind: OutputKind,
    val isList: Boolean, val count: UInt, val values: FloatArray, val parts: List<PartSnapshot> = emptyList())
data class Statistics(val fuelBudget: ULong, val fuelRemaining: ULong, val fuelUsed: ULong,
    val imageByteCount: ULong, val outputElementCount: ULong,
    val workBudget: ULong = 0u, val workRemaining: ULong = 0u, val workUsed: ULong = 0u)
/** What an auxiliary image holds: a normal map (1.1) or a material map (1.2).
 * The C tag of each constant is its ordinal + 1. */
enum class ImageKind { NORMAL, ROUGHNESS, METALLIC, AO, EMISSION }
enum class ImageSource { AUTHORED, DERIVED }

/** An auxiliary image a program can publish: an image output of its root. */
data class DeclaredImage(val index: UInt, val kind: ImageKind, val name: String)

/** Asks for a normal map derived from the finished image when the program
 * publishes none. [depth] is the height of full coverage and [rounding] a
 * Gaussian sigma, both in image pixels; [detail] is the share of brightness
 * in the height (0..1). Zero selects each default: the longer side / 16, the
 * longer side / 32 and 0.5. */
data class DerivedNormal @JvmOverloads constructor(val depth: Float = 0f, val rounding: Float = 0f, val detail: Float = 0f)

/** An image published beside the main one, owned by its result. A normal map
 * stores one direction per pixel: R is x rightward, G is y upward and B is z
 * toward the viewer, each byte b decoding as b / 127.5 - 1. Roughness,
 * metallic and AO maps hold one value in R, G and B (255 is fully rough,
 * metal, unoccluded). These are linear data, not colour; an emission map
 * holds the colour of the glow, encoded like the main image (sRGB). Alpha is
 * 255 everywhere (alphaMode == 2). */
data class AuxiliaryImage(val index: UInt, val kind: ImageKind, val source: ImageSource, val name: String,
    val width: UInt, val height: UInt, val rowStride: ULong,
    val pixelFormat: UInt, val alphaMode: UInt, val storageOrigin: UInt, val rgba: ByteArray)

data class GeneratedImage @JvmOverloads constructor(val width: UInt, val height: UInt, val rowStride: ULong,
    val pixelFormat: UInt, val alphaMode: UInt, val storageOrigin: UInt,
    val rgba: ByteArray, val outputs: List<Output>, val statistics: Statistics, val resources: List<ResultResource> = emptyList(),
    val images: List<AuxiliaryImage> = emptyList()) {
    /** The normal map published with this image, authored or derived. */
    val normal: AuxiliaryImage? get() = image(ImageKind.NORMAL)
    /** The auxiliary image of a kind published with this image, or null. */
    fun image(kind: ImageKind): AuxiliaryImage? = images.firstOrNull { it.kind == kind }
    fun output(name: String, owner: UInt = 0u): Output? = outputs.firstOrNull { it.owner == owner && it.name == name }
    fun resource(id: UInt): ResultResource? = if (id > 0u && id <= resources.size.toUInt()) resources[id.toInt() - 1] else null
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
    /** The auxiliary images this program can publish; empty for most programs. */
    val images: List<DeclaredImage>
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
            // A kind this wrapper does not know is skipped, not an error.
            images = (0 until Native.declaredImageCount(handle)).mapNotNull { index ->
                val info = Native.declaredImageInfo(handle, index)
                ImageKind.entries.getOrNull(info[1].toInt() - 1)?.let {
                    DeclaredImage(info[0].toUInt(), it, Native.declaredImageName(handle, index).toString(Charsets.UTF_8))
                }
            }
        } catch (failure: Throwable) { close(); throw failure }
    }

    /** [deriveNormal] asks the runtime for a normal map when the program
     * publishes none of its own; an authored one always wins. */
    @Synchronized
    @JvmOverloads
    fun generate(seed: UInt = 0u, inputs: Map<String, InputValue> = emptyMap(), width: UInt = 0u, height: UInt = 0u,
                 limits: ExecutionLimits = ExecutionLimits(), deriveNormal: DerivedNormal? = null): GeneratedImage {
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
            val request = deriveNormal ?: DerivedNormal()
            val invoke = { token: Long -> Native.generate(handle, seed.toInt(), width.toInt(), height.toInt(),
                limits.workLimit.toLong(), limits.timeoutNanoseconds.toLong(), token, deriveNormal != null,
                request.depth.toRawBits(), request.rounding.toRawBits(), request.detail.toRawBits()) }
            val result = limits.cancellation?.withHandle(invoke) ?: invoke(0L)
            try {
                val info = Native.imageInfo(result)
                val stats = Native.statistics(result)
                val outputs = (0 until Native.outputCount(result)).map { index ->
                    val slot = Native.outputInfo(result, index)
                    Output(index.toUInt(), slot[0].toUInt(), Native.outputName(result, index).toString(Charsets.UTF_8),
                        OutputKind.entries[slot[1].toInt()], slot[2] != 0L, slot[3].toUInt(),
                        if (slot[1] == 3L) floatArrayOf() else Native.outputValues(result, index).map { Float.fromBits(it) }.toFloatArray(),
                        if (slot[1] == 3L) readParts(result, index, slot[3].toInt()) else emptyList())
                }
                return GeneratedImage(info[0].toUInt(), info[1].toUInt(), info[2].toULong(),
                    info[3].toUInt(), info[4].toUInt(), info[5].toUInt(), Native.pixels(result), outputs,
                    Statistics(stats[0].toULong(), stats[1].toULong(), stats[2].toULong(), stats[3].toULong(), stats[4].toULong(),
                        stats[5].toULong(), stats[6].toULong(), stats[7].toULong()), readResources(result), readImages(result))
            } finally { Native.freeResult(result) }
        } finally { Native.end(handle) }
    }

    /** Owned copies of every auxiliary image, in result order. */
    private fun readImages(result: Long): List<AuxiliaryImage> = (0 until Native.auxImageCount(result)).mapNotNull { index ->
        val info = Native.auxImageInfo(result, index)
        val kind = ImageKind.entries.getOrNull(info[1].toInt() - 1)
        val source = ImageSource.entries.getOrNull(info[2].toInt() - 1)
        if (kind == null || source == null) null
        else AuxiliaryImage(info[0].toUInt(), kind, source, Native.auxImageName(result, index).toString(Charsets.UTF_8),
            info[3].toUInt(), info[4].toUInt(), info[5].toULong(), info[6].toUInt(), info[7].toUInt(), info[8].toUInt(),
            Native.auxImagePixels(result, index))
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
    external fun generate(program: Long, seed: Int, width: Int, height: Int, workLimit: Long, timeoutNanoseconds: Long, cancellation: Long,
        derive: Boolean, depthBits: Int, roundingBits: Int, detailBits: Int): Long
    external fun declaredImageCount(program: Long): Int
    external fun declaredImageInfo(program: Long, index: Int): LongArray
    external fun declaredImageName(program: Long, index: Int): ByteArray
    external fun auxImageCount(result: Long): Int
    external fun auxImageInfo(result: Long, index: Int): LongArray
    external fun auxImageName(result: Long, index: Int): ByteArray
    external fun auxImagePixels(result: Long, index: Int): ByteArray
    external fun createCancellation(): Long
    external fun cancel(cancellation: Long)
    external fun isCancelled(cancellation: Long): Boolean
    external fun freeCancellation(cancellation: Long)
    external fun freeResult(result: Long)
    external fun imageInfo(result: Long): LongArray
    external fun pixels(result: Long): ByteArray
    external fun statistics(result: Long): LongArray
    external fun outputCount(result: Long): Int
    external fun outputInfo(result: Long, index: Int): LongArray
    external fun outputName(result: Long, index: Int): ByteArray
    external fun outputValues(result: Long, index: Int): IntArray
    external fun partInfo(result: Long, slot: Int, index: Int): LongArray
    external fun resourceCount(result: Long): Int
    external fun resourceInfo(result: Long, id: Int): LongArray
    external fun resourcePixels(result: Long, id: Int): ByteArray
    external fun ringInfo(result: Long, id: Int, ring: Int): LongArray
    external fun ringVertices(result: Long, id: Int, ring: Int): IntArray
    external fun regionInfo(result: Long, id: Int, region: Int): LongArray
    external fun regionHoles(result: Long, id: Int, region: Int): IntArray
}
