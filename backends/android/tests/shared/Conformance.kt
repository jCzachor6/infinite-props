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
}
