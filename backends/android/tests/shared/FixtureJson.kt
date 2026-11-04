package dev.infiniteprops.tests

/** Test-only reader for the shared integral/bit JSON fixtures. No dependency
 * or JSON result serializer is added to the Android runtime/AAR. */
internal fun fixtureJson(source: String): Any? = FixtureJson(source).read()
private class FixtureJson(val text: String) {
    var at = 0
    fun whitespace() { while (at < text.length && text[at] in " \n\r\t") at++ }
    fun take(c: Char) { whitespace(); check(at < text.length && text[at++] == c) { "Invalid fixture JSON at $at" } }
    fun read(): Any? { val v = value(); whitespace(); check(at == text.length); return v }
    fun value(): Any? {
        whitespace(); check(at < text.length)
        return when (text[at]) {
            '{' -> {
                at++; whitespace(); val result = linkedMapOf<String, Any?>()
                if (at < text.length && text[at] == '}') { at++; result } else {
                    while (true) {
                        whitespace(); val key = string(); take(':'); check(!result.containsKey(key)); result[key] = value()
                        whitespace(); if (text[at] == '}') { at++; break }; take(',')
                    }; result
                }
            }
            '[' -> {
                at++; whitespace(); val result = mutableListOf<Any?>()
                if (at < text.length && text[at] == ']') { at++; result } else {
                    while (true) { result.add(value()); whitespace(); if (text[at] == ']') { at++; break }; take(',') }; result
                }
            }
            '"' -> string()
            't' -> literal("true", true)
            'f' -> literal("false", false)
            'n' -> literal("null", null)
            else -> {
                val begin = at
                if (text[at] == '-') at++
                check(at < text.length && text[at].isDigit())
                if (text[at] == '0') at++ else while (at < text.length && text[at] in '0'..'9') at++
                text.substring(begin, at).toLong()
            }
        }
    }
    fun literal(word: String, value: Any?): Any? { check(text.startsWith(word, at)); at += word.length; return value }
    fun string(): String {
        take('"'); val result = StringBuilder()
        while (at < text.length) {
            val c = text[at++]
            if (c == '"') return result.toString()
            if (c != '\\') { check(c.code >= 32); result.append(c); continue }
            check(at < text.length)
            when (val escaped = text[at++]) {
                '"', '\\', '/' -> result.append(escaped)
                'b' -> result.append('\b')
                'f' -> result.append('\u000c')
                'n' -> result.append('\n')
                'r' -> result.append('\r')
                't' -> result.append('\t')
                'u' -> { check(at + 4 <= text.length); result.append(text.substring(at, at + 4).toInt(16).toChar()); at += 4 }
                else -> error("Invalid fixture JSON escape")
            }
        }
        error("Unterminated fixture JSON string")
    }
}
