package dev.infiniteprops.tests

import dev.infiniteprops.*
import java.util.concurrent.Callable
import java.util.concurrent.Executors

fun verifyBindings(load: (String) -> ByteArray) {
    val program = Program(load("bindings.iprop"))
    check(program.metadata.width == 8u && program.metadata.height == 4u && program.metadata.originX == 2f)
    check(program.inputs.map { it.name } == listOf("tint", "amount", "offset"))
    val inputs = mapOf("tint" to InputValue.RGBA(Color(18, 52, 86)),
        "amount" to InputValue.F32(Float.fromBits(Int.MIN_VALUE)), "offset" to InputValue.I32(-7))
    val image = program.generate(UInt.MAX_VALUE, inputs)
    check(image.rgba.contentEquals(load("bindings.rgba"))) { "CLI RGBA oracle mismatch" }
    check(image.output("anchor")!!.values.map { it.toRawBits().toUInt() } == listOf(0x80000000u, 0xc0e00000u))
    check(image.output("marks")!!.count == 2u && image.output("unset")!!.count == 0u)
    check(image.statistics.fuelBudget - image.statistics.fuelRemaining == image.statistics.fuelUsed)
    check(image.statistics.imageByteCount == 128uL)
    check(!program.generate().rgba.contentEquals(image.rgba))
    check(program.generate(width = 16u, height = 8u).rgba.size == 512)
    for ((name, value, reason) in listOf(Triple("missing", InputValue.U32(0u), 4005), Triple("tint", InputValue.F32(1f), 4004))) {
        try { program.generate(inputs = mapOf(name to value)); error("Expected structured error") }
        catch (e: IpropException) { check(e.reason == reason && e.stage == 3) }
    }
    val expected = program.generate(17u).rgba
    val executor = Executors.newFixedThreadPool(4)
    try {
        val jobs = (0 until 20).map { executor.submit(Callable { check(program.generate(17u).rgba.contentEquals(expected)) }) }
        jobs.forEach { it.get() }
    } finally { executor.shutdown() }
    program.close(); program.close()
    check(image.rgba.size == 128 && image.output("box")!!.values.size == 4)
    try { program.generate(); error("Expected closed program") } catch (e: IllegalStateException) { check(e.message == "Program is closed") }
    try { Program(byteArrayOf()).close(); error("Expected format error") } catch (e: IpropException) { check(e.status == 2) }
    Program(load("backend-raw-values.iprop")).use { raw ->
        val result = raw.generate()
        check(result.outputs.map { it.owner } == listOf(0u, 0u, 0u, 2u, 2u, 2u))
        check(result.outputs.take(3).map { it.name } == listOf("pł\u0000suffix", "", "\u0000c\u0000\u0000"))
        check(result.outputs.take(3).flatMap { it.values.toList() }.map { it.toRawBits().toUInt() } ==
            listOf(0x80000000u, 1u, 0x7fc12345u, 0xff800000u, 0x7f800000u, 0xffc12345u, 0x80000001u, 0x3f800000u, 0x80000000u))
        check(result.outputs.drop(3).all { it.count == 0u })
    }
    Program(load("binding-unicode.iprop")).use { names ->
        check(names.inputs[0].name == "pł\u0000suffix")
        check(names.generate(inputs = mapOf("pł\u0000suffix" to InputValue.RGBA(Color(18, 52, 86)))).rgba.contentEquals(byteArrayOf(18, 52, 86, -1)))
        check(names.generate(inputs = mapOf("é" to InputValue.F32(1f), "e\u0301" to InputValue.F32(2f))).output("anchor")!!.values.contentEquals(floatArrayOf(1f, 2f)))
        try { names.generate(inputs = mapOf("pł\u0000suffix\u0000extra" to InputValue.RGBA(Color(0, 0, 0)))); error("Expected unknown name") }
        catch (e: IpropException) { check(e.reason == 4005) }
    }
    Program(load("binding-trap.iprop")).use { trap ->
        try { trap.generate(); error("Expected runtime trap") }
        catch (e: IpropException) { check(e.status == 3 && e.reason == 3008 && e.stage == 4 && (e.locationFlags and 8) != 0 && e.debugLine > 0) }
        check(trap.generate(inputs = mapOf("divisor" to InputValue.I32(1))).output("value")!!.values.contentEquals(floatArrayOf(1f, 0f)))
    }
    Program(load("execution-slow.iprop")).use { slow ->
        val token = CancellationToken()
        check(!token.isCancelled)
        val thread = Thread { Thread.sleep(10); token.cancel(); token.close() }
        thread.start()
        try {
            try { slow.generate(limits = ExecutionLimits(workLimit = 1_000_000_000u, cancellation = token)); error("Cancellation was ignored") }
            catch (failure: IpropException) { check(failure.reason == 3016 && failure.stage == 4 && (failure.locationFlags and 4) != 0) }
        } finally { thread.join(); token.close() }
        try { token.cancel(); error("Closed token accepted") } catch (_: IllegalStateException) { }
        CancellationToken().use { cancelled ->
            cancelled.cancel(); check(cancelled.isCancelled)
            repeat(32) {
                try { slow.generate(limits = ExecutionLimits(cancellation = cancelled)); error("Pre-cancel ignored") }
                catch (failure: IpropException) { check(failure.reason == 3016) }
            }
        }
        check(slow.generate(inputs = mapOf("enabled" to InputValue.I32(0))).width == 1u)
    }
    println("Kotlin/JNI execution limits: PASS work/deadline, active cancellation, deferred token close and recovery")
    verifyNormalMaps(load)

}

/** Auxiliary images against the CLI-rendered shared fixtures. */
fun verifyNormalMaps(load: (String) -> ByteArray) {
    Program(load("binding-normal.iprop")).use { program ->
        check(program.images == listOf(DeclaredImage(0u, ImageKind.NORMAL, "relief")))
        val result = program.generate(5u)
        check(result.rgba.contentEquals(load("binding-normal.rgba"))) { "CLI RGBA oracle mismatch" }
        val normal = result.normal!!
        check(result.images.size == 1 && result.images[0] === normal)
        check(normal.index == 0u && normal.kind == ImageKind.NORMAL && normal.source == ImageSource.AUTHORED && normal.name == "relief")
        check(normal.width == 8u && normal.height == 4u && normal.rowStride == 32uL)
        check(normal.pixelFormat == result.pixelFormat && normal.alphaMode == 2u && normal.storageOrigin == result.storageOrigin)
        check(normal.rgba.contentEquals(load("binding-normal.normal.rgba"))) { "CLI normal map oracle mismatch" }
        check((3 until normal.rgba.size step 4).all { normal.rgba[it] == (-1).toByte() })
        // The seed moves one painted direction; a size override scales the image.
        check(!program.generate(6u).normal!!.rgba.contentEquals(normal.rgba))
        val scaled = program.generate(5u, width = 16u, height = 8u).normal!!
        check(scaled.width == 16u && scaled.height == 8u && scaled.rgba.size == 512)
        // An authored image wins over a derive request and stays identical.
        val requested = program.generate(5u, deriveNormal = DerivedNormal(depth = 9f)).normal!!
        check(requested.source == ImageSource.AUTHORED && requested.rgba.contentEquals(normal.rgba))
    }
    Program(load("bindings.iprop")).use { program ->
        check(program.images.isEmpty())
        val plain = program.generate(5u)
        check(plain.images.isEmpty() && plain.normal == null)
        for ((request, name) in listOf(DerivedNormal() to "bindings-derived",
                DerivedNormal(depth = 2f, rounding = 0.5f, detail = 0.25f) to "bindings-derived-custom")) {
            val result = program.generate(5u, deriveNormal = request)
            val normal = result.normal!!
            check(normal.kind == ImageKind.NORMAL && normal.source == ImageSource.DERIVED && normal.name == "normal")
            check(normal.rgba.contentEquals(load("$name.normal.rgba"))) { "CLI derived normal oracle mismatch: $name" }
            check(normal.width == 8u && normal.height == 4u && normal.alphaMode == 2u)
            // The request leaves the main image and fuel alone and spends work.
            check(result.rgba.contentEquals(plain.rgba) && result.statistics.fuelUsed == plain.statistics.fuelUsed)
            check(result.statistics.workUsed > plain.statistics.workUsed)
        }
        val used = program.generate(5u, deriveNormal = DerivedNormal()).statistics.workUsed
        check(program.generate(5u, limits = ExecutionLimits(workLimit = used), deriveNormal = DerivedNormal()).statistics.workRemaining == 0uL)
        try { program.generate(5u, limits = ExecutionLimits(workLimit = used - 1u), deriveNormal = DerivedNormal()); error("Work limit was ignored") }
        catch (failure: IpropException) { check(failure.reason == 3015 && failure.stage == 4) }
        for (request in listOf(DerivedNormal(depth = -1f), DerivedNormal(depth = Float.POSITIVE_INFINITY), DerivedNormal(rounding = 64.5f),
                DerivedNormal(rounding = -0.5f), DerivedNormal(detail = 1.5f), DerivedNormal(detail = Float.NaN))) {
            try { program.generate(5u, deriveNormal = request); error("Expected an argument error") }
            catch (failure: IpropException) { check(failure.status == 4 && failure.stage == 3 && failure.message!!.contains("out of range")) }
        }
        check(program.generate(5u).normal == null)
    }
    println("Kotlin/JNI normal maps: PASS authored and derived images against the CLI, declarations, work limits and argument errors")
}
