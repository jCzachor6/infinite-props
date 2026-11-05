package dev.infiniteprops

/** Independent native work and monotonic timeout. Zero work uses the finite
 * default; zero timeout disables the clock. */
data class ExecutionLimits(val workLimit: ULong = 0u, val timeoutNanoseconds: ULong = 0u,
    val cancellation: CancellationToken? = null)

/** One-way native cancellation. Its lock is independent of Program.generate;
 * close defers native destruction until active generation leases finish. */
class CancellationToken : AutoCloseable {
    private val gate = Any()
    private var handle = Native.createCancellation()
    private var leases = 0
    private var closed = false

    fun cancel() = synchronized(gate) {
        check(!closed) { "Cancellation token is closed" }
        Native.cancel(handle)
    }
    val isCancelled: Boolean get() = synchronized(gate) {
        check(!closed) { "Cancellation token is closed" }
        Native.isCancelled(handle)
    }
    internal fun <T> withHandle(body: (Long) -> T): T {
        val pointer = synchronized(gate) {
            check(!closed) { "Cancellation token is closed" }
            ++leases; handle
        }
        try { return body(pointer) }
        finally { synchronized(gate) { --leases; releaseIfClosed() } }
    }
    private fun releaseIfClosed() {
        if (closed && leases == 0 && handle != 0L) { Native.freeCancellation(handle); handle = 0L }
    }
    override fun close() = synchronized(gate) { closed = true; releaseIfClosed() }
    @Suppress("deprecation")
    protected fun finalize() { close() }
}
