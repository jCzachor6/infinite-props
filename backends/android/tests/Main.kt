import dev.infiniteprops.tests.verifyBindings
import dev.infiniteprops.tests.verifyPixelCorpus
import dev.infiniteprops.tests.verifyPartOutputs
import java.io.File

fun main(args: Array<String>) {
    verifyBindings { File(args[0], it).readBytes() }
    verifyPartOutputs { File(args[0], it).readBytes() }
    verifyPixelCorpus(load = { File(args[1], it).readBytes() })
    println("Kotlin / JNI native conformance passed")
}
