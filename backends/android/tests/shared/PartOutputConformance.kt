package dev.infiniteprops.tests

import dev.infiniteprops.*

private fun bits(f: Float) = f.toRawBits().toUInt().toLong()
private fun point(p: Point) = listOf(bits(p.x), bits(p.y))
private fun observation(result: GeneratedImage): Map<String, Any> {
    val outputs = result.outputs.mapIndexed { index, s ->
        check(s.index == index.toUInt())
        val stride = listOf(2, 4, 3, 0)[s.kind.ordinal]
        check(s.count.toInt() == if (stride == 0) s.parts.size else s.values.size / stride)
        val parts = s.parts.map { p ->
            check(result.resource(p.shapeResource) === result.resources[p.shapeResource.toInt() - 1])
            if (p.textureResource != 0u) check(result.resource(p.textureResource) != null)
            mapOf("slot" to p.outputIndex.toLong(), "entry" to p.elementIndex.toLong(), "id" to p.id.toLong(), "source_id" to p.sourceId.toLong(),
                "shape" to listOf(p.shapeKind.toLong(), p.shapeResource.toLong(), p.regionIndex.toLong()),
                "appearance" to listOf(p.appearanceKind.toLong(), p.textureResource.toLong(), p.tint.r.toLong(), p.tint.g.toLong(), p.tint.b.toLong(), p.tint.a.toLong()),
                "source_mapping" to listOf(p.sourceD4.toLong(), p.sourceOriginX.toLong(), p.sourceOriginY.toLong()),
                "texture_mapping" to listOf(p.textureD4.toLong(), p.textureOriginX.toLong(), p.textureOriginY.toLong()),
                "transform" to listOf(p.d4.toLong(), p.translationX.toLong(), p.translationY.toLong()),
                "source_scale" to point(p.sourceScale), "target_scale" to point(p.targetScale), "anchor" to point(p.sourceAnchor),
                "center" to point(p.center), "bounds_origin" to point(p.boundsOrigin), "bounds_size" to point(p.boundsSize))
        }
        mapOf("name" to s.name, "owner" to s.owner.toLong(), "kind" to s.kind.ordinal.toLong(), "list" to s.isList,
            "values" to if (stride == 0) emptyList<List<Long>>() else s.values.map(::bits).chunked(stride), "parts" to parts)
    }
    val resources = result.resources.mapIndexed { index, r ->
        check(r.id == (index + 1).toUInt() && r.byteCount == r.data.size.toULong())
        if (r.kind == 2u) mapOf("kind" to "raster", "format" to r.pixelFormat.toLong(), "size" to listOf(r.width.toLong(), r.height.toLong()),
            "scale" to point(r.scale), "stride" to r.rowStride.toLong(), "bytes" to r.data.map { (it.toInt() and 255).toLong() })
        else {
            check(r.kind == 1u)
            mapOf("kind" to "geometry", "vertex_count" to r.vertexCount.toLong(), "edge_count" to r.edgeCount.toLong(),
                "rings" to r.rings.mapIndexed { i, v ->
                    check(v.index == i.toUInt()); mapOf("parent" to v.parentRing.toLong(), "hole" to v.isHole, "vertices" to v.vertices.map(::point))
                }, "regions" to r.regions.mapIndexed { i, v ->
                    check(v.index == i.toUInt()); mapOf("outer" to v.outerRing.toLong(), "holes" to v.holes.map { it.toLong() },
                        "source_id" to v.sourceId.toLong(), "area" to bits(v.area), "center" to point(v.center),
                        "bounds_origin" to point(v.boundsOrigin), "bounds_size" to point(v.boundsSize))
                })
        }
    }
    return mapOf("schema" to 1L, "size" to listOf(result.width.toLong(), result.height.toLong()),
        "rgba" to result.rgba.map { (it.toInt() and 255).toLong() }, "outputs" to outputs, "resources" to resources)
}

fun verifyPartOutputs(load: (String) -> ByteArray) {
    val cases = fixtureJson(load("part-bindings.json").toString(Charsets.UTF_8)) as List<*>
    check(cases.size == 23)
    for (item in cases) {
        val c = item as Map<*, *>
        val directory = c["directory"] as String
        val expected = fixtureJson(load("$directory/expected.json").toString(Charsets.UTF_8))
        val size = c["size"] as List<*>
        val seed = (c["seed"] as Long).toUInt()
        val result: GeneratedImage
        Program(load("$directory/root.iprop")).use { program ->
            result = program.generate(seed, width = (size[0] as Long).toUInt(), height = (size[1] as Long).toUInt())
            check(observation(result) == expected) { "Complete C++/C part mismatch: ${c["name"]}" }
            val used = result.statistics.workUsed
            check(used > 0u && result.statistics.workBudget == 100_000_000uL)
            val exact = program.generate(seed, width = (size[0] as Long).toUInt(), height = (size[1] as Long).toUInt(),
                limits = ExecutionLimits(workLimit = used, timeoutNanoseconds = 60_000_000_000u))
            check(exact.statistics.workRemaining == 0uL && exact.statistics.fuelUsed == result.statistics.fuelUsed)
            check(observation(exact) == expected) { "Exact work changed typed results" }
            for ((limits, reason) in listOf(ExecutionLimits(workLimit = used - 1u) to 3015,
                    ExecutionLimits(timeoutNanoseconds = 1u) to 3017)) {
                try { program.generate(seed, width = (size[0] as Long).toUInt(), height = (size[1] as Long).toUInt(), limits = limits); error("Expected work/deadline error") }
                catch (failure: IpropException) { check(failure.reason == reason && failure.stage == 4) }
            }
            check(observation(program.generate(seed, width = (size[0] as Long).toUInt(), height = (size[1] as Long).toUInt())) == expected)
            program.generate(seed xor UInt.MAX_VALUE, width = (size[0] as Long).toUInt(), height = (size[1] as Long).toUInt())
        }
        check(observation(result) == expected) { "Retained part result: ${c["name"]}" }
        check(result.statistics.fuelUsed == result.statistics.fuelBudget - result.statistics.fuelRemaining)
        check(result.statistics.outputElementCount == result.outputs.sumOf { it.count.toULong() })
        check(result.resource(0u) == null && result.resource(result.resources.size.toUInt() + 1u) == null)
    }
    println("Kotlin/JNI typed parts: PASS 23 complete C++/C fixtures and independent result lifetime")
}
