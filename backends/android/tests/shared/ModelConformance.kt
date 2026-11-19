package dev.infiniteprops.tests

import dev.infiniteprops.*
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.concurrent.Executors

fun verifyModels(load: (String) -> ByteArray) {
    val data = load("model-corpus.bin")
    val sha = MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }
    check(load("model-corpus.json").decodeToString().contains(sha))
    val r = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
    fun blob(): ByteArray {
        val size = r.int
        check(size in 0..r.remaining())
        return ByteArray(size).also { r.get(it) }
    }
    fun hash(bytes: ByteArray) = bytes.fold(0xcbf29ce484222325uL) { h, b -> (h xor b.toUByte().toULong()) * 0x100000001b3uL }
    fun floats(array: FloatArray) {
        check(r.int == 4 * array.size)
        for (f in array) check(r.int == f.toRawBits())
    }
    check(ByteArray(8).also { r.get(it) }.decodeToString() == "IPMCRP01")
    val models = List(r.int) { Model(blob()) }
    try {
        repeat(r.int) {
            val label = blob().decodeToString()
            val model = models[r.int]
            val seed = r.int.toUInt()
            val lod = r.int
            val inputs = mutableMapOf<String, InputValue>()
            repeat(r.int) {
                val name = blob().decodeToString()
                val kind = r.int
                val bits = r.int
                inputs[name] = when (kind) {
                    0 -> InputValue.RGBA(Color(bits and 255, bits ushr 8 and 255, bits ushr 16 and 255, bits ushr 24))
                    1 -> InputValue.F32(Float.fromBits(bits))
                    2 -> InputValue.U32(bits.toUInt())
                    else -> error("Invalid input type in $label")
                }
            }
            val m = model.generate(seed, inputs, lod)
            for (f in m.size + m.unitsPerMeter + m.boundsMin + m.boundsMax) check(f.toRawBits() == r.int) { label }
            val s = m.statistics
            check(s.vertexCount == r.int.toUInt() && s.triangleCount == r.int.toUInt())
            for (n in listOf(s.fuelBudget, s.fuelUsed, s.workBudget, s.workUsed)) check(n == r.long.toULong())
            check(m.textures.size == r.int)
            for (t in m.textures) {
                check(t.key == r.long.toULong() && t.name == blob().decodeToString())
                for (v in listOf(t.width, t.height, t.assetWidth, t.assetHeight)) check(v == r.int.toUInt())
                check(t.repeats == (r.int != 0) && hash(t.rgba) == r.long.toULong())
                check(t.images.size == r.int)
                for (a in t.images) check(a.kind.ordinal + 1 == r.int && a.name == blob().decodeToString() && hash(a.rgba) == r.long.toULong())
            }
            check(m.surfaces.size == r.int)
            for (surface in m.surfaces) {
                check(surface.textureIndex == r.int)
                for (f in listOf(surface.roughness, surface.metallic, surface.emission)) check(f.toRawBits() == r.int)
                check(surface.alphaMode == r.int && surface.alphaCutoff.toRawBits() == r.int)
                check(surface.twoSided == (r.int != 0) && surface.crisp == (r.int != 0))
                for (array in listOf(surface.positions, surface.normals, surface.tangents, surface.uvs)) floats(array)
                check(surface.indices.size * 4 == r.int)
                for (i in surface.indices) check(i == r.int)
            }
            check(m.outputs.size == r.int)
            for (o in m.outputs) {
                check(o.name == blob().decodeToString() && o.kind == r.int && o.isList == (r.int != 0) && o.valueCount == r.int)
                floats(o.values)
            }
        }
        check(!r.hasRemaining())
    } finally { models.forEach { it.close() } }
    Model(load("model-crate-debug.iprop3d")).use { model ->
        check(model.metadata.size.contentEquals(floatArrayOf(12f,10f,8f)) && model.metadata.unitsPerMeter == 50f)
        check(model.inputs.map { it.name } == listOf("wide","stain","extra","rounding"))
        check(model.skinPrograms.single().name == "tile")
        Program(load("model-tile.iprop")).use { tile ->
            val expected = tile.generate(77u, mapOf("tint" to InputValue.RGBA(Color(51,102,153))), 32u, 16u)
            val t = model.generate(5u).textures.first()
            check(expected.rgba.contentEquals(t.rgba) && expected.images.single().rgba.contentEquals(t.images.single().rgba))
        }
        fun failure(reason: Int, call: () -> Unit) {
            val error = runCatching(call).exceptionOrNull() as IpropException
            check(error.reason.toInt() == reason)
            if (reason == 3010) check(error.debugLine.toInt() > 0)
            if (reason == 3015) check(error.message!!.contains("skin 'tile'"))
            model.generate()
        }
        failure(3010) { model.generate(inputs = mapOf("wide" to InputValue.F32(0f))) }
        failure(4004) { model.generate(inputs = mapOf("wide" to InputValue.U32(1u))) }
        failure(4005) { model.generate(inputs = mapOf("missing" to InputValue.U32(1u))) }
        failure(3015) { model.generate(limits = ExecutionLimits(workLimit = 200u)) }
        CancellationToken().use { token ->
            token.cancel()
            failure(3016) { model.generate(limits = ExecutionLimits(cancellation = token)) }
        }
    }
    Model(load("model-lod.iprop3d")).use { m ->
        val cache = TextureCache()
        val cold = m.generate(textures = cache)
        val warm = m.generate(textures = cache)
        check(warm.textures.all { it.reused && it.rgba === cold.textures[it.index].rgba })
        check(m.generate(known = cache.known).textures.all { it.reused && it.rgba.isEmpty() })
        val pool = Executors.newFixedThreadPool(8)
        try {
            (0 until 48).map { i -> pool.submit {
                val seed = (100 + i % 8).toUInt()
                val lod = (i % 8) % 3
                val a = m.generate(seed, lod = lod, textures = cache)
                val b = m.generate(seed, lod = lod)
                check(a.surfaces.zip(b.surfaces).all { (s,t) -> s.positions.contentEquals(t.positions) })
                check(a.textures.zip(b.textures).all { (s,t) -> s.rgba.contentEquals(t.rgba) })
            } }.forEach { it.get() }
        } finally { pool.shutdown() }
        check(runCatching { m.generate(lod = 8) }.exceptionOrNull() is IpropException)
        m.close()
        check(runCatching { m.generate() }.exceptionOrNull() is IllegalStateException)
        check(cold.textures.first().rgba.isNotEmpty())
    }
}
