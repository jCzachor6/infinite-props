// The .ips language definition for CodeMirror 6: a stream-based tokenizer
// mirroring the real lexer's token set (keywords, type names, output kinds,
// literals, comments and operators).

import { StreamLanguage } from "@codemirror/language";

const KEYWORDS = [
    "asset", "input", "output", "import", "from", "draw", "at",
    "if", "else", "while", "return", "true", "false", "list",
];

const TYPES = ["color", "f32", "u32", "i32", "point", "rect", "circle"];

export const ipsLanguage = StreamLanguage.define({
    startState: () => ({ inBlockComment: false }),
    token(stream, state) {
        if (state.inBlockComment) {
            if (stream.match("*/")) {
                state.inBlockComment = false;
                return "comment";
            }
            stream.next();
            return "comment";
        }
        if (stream.eatSpace()) return null;
        if (stream.match("//")) {
            stream.skipToEnd();
            return "comment";
        }
        if (stream.match("/*")) {
            state.inBlockComment = true;
            return "comment";
        }
        if (stream.peek() === '"') {
            stream.next();
            while (!stream.eol()) {
                const ch = stream.next();
                if (ch === "\\") stream.next();
                else if (ch === '"') break;
            }
            return "string";
        }
        if (stream.peek() === "#") {
            stream.match(/#[0-9a-fA-F]*/);
            return "atom";
        }
        if (stream.match(/0[xX][0-9a-fA-F]+/) ||
            stream.match(/\d+(\.\d+)?([eE][+-]?\d+)?/)) {
            return "number";
        }
        if (stream.match(/->|==|!=|<=|>=|&&|\|\||<<|>>/)) return "operator";
        if (stream.match(/[+\-*/%^&|~!<>=]/)) return "operator";
        if (stream.match(/[{}()[\],.:;]/)) return "punctuation";
        if (stream.match(/\w+/)) {
            const word = stream.current();
            if (KEYWORDS.includes(word)) return "keyword";
            if (TYPES.includes(word)) return "typeName";
            return "variableName";
        }
        stream.next();
        return null;
    },
});
