package dev.infiniteprops

data class ModelMetadata(val size: FloatArray, val unitsPerMeter: Float, val fuel: ULong, val containerFlags: UInt)
data class ModelLibrary(val apiMajor: UInt, val apiMinor: UInt, val maxVertices: UInt, val maxTriangles: UInt,
                        val maxTextures: UInt, val maxTextureSide: UInt, val maxTexels: ULong, val textureStamp: ULong,
                        val maxLod: Int = 7)
data class SkinProgram(val index: Int, val width: Int, val height: Int, val name: String)
data class KnownTexture(val key: ULong, val fuel: ULong, val work: ULong, val imageKinds: UInt) {
    val keyHex get() = key.toString(16).padStart(16, '0')
}
data class ModelTexture(val known: KnownTexture, val index: Int, val name: String, val assetWidth: UInt, val assetHeight: UInt,
                        val width: UInt, val height: UInt, val repeats: Boolean, val reused: Boolean, val rowStride: ULong,
                        val pixelFormat: UInt, val alphaMode: UInt, val origin: UInt, val rgba: ByteArray, val images: List<AuxiliaryImage>) {
    val key get() = known.key
}
data class Surface(val index: Int, val textureIndex: Int, val roughness: Float, val metallic: Float, val emission: Float,
                   val alphaMode: Int, val alphaCutoff: Float, val twoSided: Boolean, val crisp: Boolean,
                   val positions: FloatArray, val normals: FloatArray, val tangents: FloatArray, val uvs: FloatArray, val indices: IntArray)
data class ModelOutput(val index: Int, val name: String, val kind: Int, val isList: Boolean, val valueCount: Int,
                       val floatsPerValue: Int, val values: FloatArray)
data class ModelStatistics(val vertexCount: UInt, val triangleCount: UInt, val fuelBudget: ULong, val fuelUsed: ULong,
                           val workBudget: ULong, val workUsed: ULong)
data class GeneratedModel(val size: FloatArray, val unitsPerMeter: Float, val boundsMin: FloatArray, val boundsMax: FloatArray,
                          val statistics: ModelStatistics, val surfaces: List<Surface>, val textures: List<ModelTexture>, val outputs: List<ModelOutput>)

class TextureCache {
    private val entries = mutableMapOf<ULong, ModelTexture>()
    @Synchronized internal fun snapshot() = entries.toMap()
    val known get() = snapshot().values.map { it.known }
    val size get() = synchronized(this) { entries.size }
    @Synchronized operator fun get(key: ULong) = entries[key]
    @Synchronized fun remove(key: ULong) = entries.remove(key)
    @Synchronized fun clear() = entries.clear()
    @Synchronized internal fun store(textures: List<ModelTexture>) {
        for (texture in textures) if (!texture.reused) entries[texture.key] = texture
    }
}

private fun LongArray.f(i: Int) = Float.fromBits(this[i].toInt())
private fun LongArray.triple(i: Int) = floatArrayOf(f(i), f(i + 1), f(i + 2))

class Model(bytes: ByteArray) : AutoCloseable {
    companion object {
        fun library(): ModelLibrary {
            val v = ModelNative.library()
            return ModelLibrary(v[0].toUInt(), v[1].toUInt(), v[2].toUInt(), v[3].toUInt(), v[4].toUInt(), v[5].toUInt(),
                                v[6].toULong(), v[7].toULong())
        }
    }
    private var handle = 0L
    private var leases = 0
    private var closed = false
    val metadata: ModelMetadata
    val inputs: List<Input>
    val skinPrograms: List<SkinProgram>
    init {
        library()
        handle = ModelNative.load(bytes)
        try {
            val v = ModelNative.metadata(handle)
            metadata = ModelMetadata(v.triple(0), v.f(3), v[4].toULong(), v[5].toUInt())
            inputs = List(v[6].toInt()) { i ->
                val a = ModelNative.inputInfo(handle, i)
                val value = when (a[0].toInt()) {
                    0 -> InputValue.RGBA(Color.fromBits(a[1].toInt()))
                    1 -> InputValue.F32(a.f(1))
                    else -> InputValue.U32(a[1].toUInt())
                }
                Input(i.toUInt(), ModelNative.inputName(handle, i).decodeToString(), value)
            }
            skinPrograms = List(v[7].toInt()) { i ->
                val a = ModelNative.skinInfo(handle, i)
                SkinProgram(i, a[0].toInt(), a[1].toInt(), ModelNative.skinName(handle, i).decodeToString())
            }
        } catch (error: Throwable) { close(); throw error }
    }
    private fun releaseIfClosed() {
        if (closed && leases == 0 && handle != 0L) { ModelNative.close(handle); handle = 0L }
    }
    @Synchronized override fun close() { closed = true; releaseIfClosed() }
    @Suppress("deprecation")
    protected fun finalize() { close() }

    @JvmOverloads fun generate(seed: UInt = 0u, inputs: Map<String, InputValue> = emptyMap(), lod: Int = 0,
                               textures: TextureCache? = null, known: List<KnownTexture> = emptyList(),
                               limits: ExecutionLimits = ExecutionLimits()): GeneratedModel {
        val held = synchronized(this) {
            check(!closed) { "Model is closed" }
            ++leases
            handle
        }
        try {
            val p = ModelNative.parameters(held)
            try {
                for ((name, value) in inputs) {
                    val (type, bits) = when (value) {
                        is InputValue.RGBA -> 0 to value.value.bits
                        is InputValue.F32 -> 1 to value.value.toRawBits()
                        is InputValue.U32 -> 2 to value.value.toInt()
                        is InputValue.I32 -> 2 to value.value
                    }
                    ModelNative.set(p, name.encodeToByteArray(), type, bits)
                }
                val snapshot = textures?.snapshot()
                val records = (snapshot?.values?.map { it.known }.orEmpty() + known).flatMap {
                    listOf(it.key.toLong(), it.fuel.toLong(), it.work.toLong(), it.imageKinds.toLong())
                }.toLongArray()
                val invoke = { token: Long -> ModelNative.generate(held, p, seed.toInt(), lod, limits.workLimit.toLong(),
                    limits.timeoutNanoseconds.toLong(), token, records) }
                val result = limits.cancellation?.withHandle(invoke) ?: invoke(0L)
                try {
                    val model = copyModel(result, snapshot)
                    textures?.store(model.textures)
                    return model
                } finally { ModelNative.releaseResult(result) }
            } finally { ModelNative.releaseParameters(p) }
        } finally { synchronized(this) { --leases; releaseIfClosed() } }
    }
}

private fun copyModel(result: Long, cache: Map<ULong, ModelTexture>?): GeneratedModel {
    val r = ModelNative.statistics(result)
    val stats = ModelStatistics(r[10].toUInt(), r[11].toUInt(), r[12].toULong(), r[13].toULong(), r[14].toULong(), r[15].toULong())
    val surfaces = List(r[16].toInt()) { i ->
        val s = ModelNative.surfaceInfo(result, i)
        Surface(i, s[0].toInt(), s.f(1), s.f(2), s.f(3), s[4].toInt(), s.f(5), s[6] != 0L, s[7] != 0L,
            ModelNative.positions(result, i), ModelNative.normals(result, i), ModelNative.tangents(result, i),
            ModelNative.uvs(result, i), ModelNative.indices(result, i))
    }
    val textures = List(r[17].toInt()) { i ->
        val t = ModelNative.textureInfo(result, i)
        val key = KnownTexture(t[0].toULong(), t[1].toULong(), t[2].toULong(), t[3].toUInt())
        val reused = t[9] != 0L
        val stored = if (reused) cache?.getValue(key.key) else null
        val images = stored?.images ?: List(t[14].toInt()) { j ->
            val a = ModelNative.imageInfo(result, i, j)
            AuxiliaryImage(j.toUInt(), ImageKind.entries[a[0].toInt()-1], ImageSource.entries[a[1].toInt()-1],
                ModelNative.imageName(result, i, j).decodeToString(), a[2].toUInt(), a[3].toUInt(), a[4].toULong(),
                a[5].toUInt(), a[6].toUInt(), a[7].toUInt(), ModelNative.imagePixels(result, i, j))
        }
        ModelTexture(key, i, ModelNative.textureName(result, i).decodeToString(), t[4].toUInt(), t[5].toUInt(),
            t[6].toUInt(), t[7].toUInt(), t[8] != 0L, reused, stored?.rowStride ?: t[10].toULong(),
            stored?.pixelFormat ?: t[11].toUInt(), stored?.alphaMode ?: t[12].toUInt(), stored?.origin ?: t[13].toUInt(),
            stored?.rgba ?: ModelNative.texturePixels(result, i), images)
    }
    val outputs = List(r[18].toInt()) { i ->
        val o = ModelNative.outputInfo(result, i)
        ModelOutput(i, ModelNative.outputName(result, i).decodeToString(), o[0].toInt(), o[1] != 0L, o[2].toInt(),
                    o[3].toInt(), ModelNative.outputValues(result, i))
    }
    return GeneratedModel(r.triple(0), r.f(3), r.triple(4), r.triple(7), stats, surfaces, textures, outputs)
}

internal object ModelNative {
    init { System.loadLibrary("iprop_jni") }
    external fun library(): LongArray
    external fun load(bytesIn: ByteArray): Long
    external fun close(raw: Long): Unit
    external fun releaseResult(raw: Long): Unit
    external fun releaseParameters(raw: Long): Unit
    external fun parameters(raw: Long): Long
    external fun set(raw: Long, name: ByteArray, type: Int, value: Int): Unit
    external fun generate(raw: Long, parameters: Long, seed: Int, lod: Int, work: Long, timeout: Long, token: Long, records: LongArray): Long
    external fun metadata(raw: Long): LongArray
    external fun inputInfo(raw: Long, i: Int): LongArray
    external fun skinInfo(raw: Long, i: Int): LongArray
    external fun statistics(raw: Long): LongArray
    external fun surfaceInfo(raw: Long, i: Int): LongArray
    external fun textureInfo(raw: Long, i: Int): LongArray
    external fun imageInfo(raw: Long, i: Int, j: Int): LongArray
    external fun outputInfo(raw: Long, i: Int): LongArray
    external fun skinName(raw: Long, i: Int): ByteArray
    external fun inputName(raw: Long, i: Int): ByteArray
    external fun textureName(raw: Long, i: Int): ByteArray
    external fun texturePixels(raw: Long, i: Int): ByteArray
    external fun imageName(raw: Long, i: Int, j: Int): ByteArray
    external fun imagePixels(raw: Long, i: Int, j: Int): ByteArray
    external fun outputName(raw: Long, i: Int): ByteArray
    external fun positions(raw: Long, i: Int): FloatArray
    external fun normals(raw: Long, i: Int): FloatArray
    external fun tangents(raw: Long, i: Int): FloatArray
    external fun uvs(raw: Long, i: Int): FloatArray
    external fun indices(raw: Long, i: Int): IntArray
    external fun outputValues(raw: Long, i: Int): FloatArray
}
