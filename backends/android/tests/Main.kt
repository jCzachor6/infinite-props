import dev.infiniteprops.tests.verifyBindings
import java.io.File

fun main(args: Array<String>) {
    verifyBindings { File(args[0], it).readBytes() }
    println("Kotlin / JNI native conformance passed")
}
