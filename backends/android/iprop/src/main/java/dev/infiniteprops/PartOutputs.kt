package dev.infiniteprops

data class Point(val x: Float, val y: Float)
/** Complete checked C snapshot. D4/translation already include imports and
 * rounding. Resource IDs belong to this result; UInt.MAX_VALUE means no region. */
data class PartSnapshot(val outputIndex: UInt, val elementIndex: UInt, val id: Int, val sourceId: Int,
    val shapeKind: UInt, val appearanceKind: UInt, val shapeResource: UInt, val regionIndex: UInt,
    val textureResource: UInt, val tint: Color, val d4: UInt, val translationX: Int, val translationY: Int,
    val sourceScale: Point, val targetScale: Point, val sourceAnchor: Point, val center: Point,
    val boundsOrigin: Point, val boundsSize: Point, val sourceD4: UInt, val sourceOriginX: Int,
    val sourceOriginY: Int, val textureD4: UInt, val textureOriginX: Int, val textureOriginY: Int)
data class ResourceRing(val index: UInt, val parentRing: UInt, val isHole: Boolean, val vertices: List<Point>)
data class ResourceRegion(val index: UInt, val outerRing: UInt, val holes: List<UInt>, val sourceId: Int,
    val area: Float, val center: Point, val boundsOrigin: Point, val boundsSize: Point)
/** One owned copy per ID, shared by snapshot references. No native view escapes. */
data class ResultResource(val id: UInt, val kind: UInt, val pixelFormat: UInt, val width: UInt, val height: UInt,
    val rowStride: ULong, val data: ByteArray, val scale: Point, val vertexCount: UInt, val edgeCount: UInt,
    val rings: List<ResourceRing>, val regions: List<ResourceRegion>) {
    val byteCount: ULong get() = data.size.toULong()
}

private fun LongArray.point(at: Int) = Point(Float.fromBits(this[at].toInt()), Float.fromBits(this[at + 1].toInt()))

internal fun readParts(result: Long, slot: Int, count: Int): List<PartSnapshot> = (0 until count).map { index ->
    val p = Native.partInfo(result, slot, index)
    PartSnapshot(p[0].toUInt(), p[1].toUInt(), p[2].toInt(), p[3].toInt(), p[4].toUInt(), p[5].toUInt(),
        p[6].toUInt(), p[7].toUInt(), p[8].toUInt(), Color.fromBits(p[9].toInt()), p[10].toUInt(), p[11].toInt(), p[12].toInt(),
        p.point(13), p.point(15), p.point(17), p.point(19), p.point(21), p.point(23),
        p[25].toUInt(), p[26].toInt(), p[27].toInt(), p[28].toUInt(), p[29].toInt(), p[30].toInt())
}

internal fun readResources(result: Long): List<ResultResource> = (1..Native.resourceCount(result)).map { id ->
    val r = Native.resourceInfo(result, id)
    val rings = (0 until r[11].toInt()).map { index ->
        val v = Native.ringInfo(result, id, index)
        val bits = Native.ringVertices(result, id, index)
        ResourceRing(v[0].toUInt(), v[2].toUInt(), v[3] != 0L,
            (bits.indices step 2).map { Point(Float.fromBits(bits[it]), Float.fromBits(bits[it + 1])) })
    }
    val regions = (0 until r[12].toInt()).map { index ->
        val v = Native.regionInfo(result, id, index)
        ResourceRegion(v[0].toUInt(), v[1].toUInt(), Native.regionHoles(result, id, index).map { it.toUInt() },
            v[3].toInt(), Float.fromBits(v[4].toInt()), v.point(5), v.point(7), v.point(9))
    }
    ResultResource(r[0].toUInt(), r[1].toUInt(), r[2].toUInt(), r[3].toUInt(), r[4].toUInt(), r[5].toULong(),
        if (r[6] == 0L) byteArrayOf() else Native.resourcePixels(result, id), r.point(7), r[9].toUInt(), r[10].toUInt(), rings, regions)
}
