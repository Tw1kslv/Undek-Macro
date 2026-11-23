#include "script/Lexer.h"
#include "script/Parser.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

// A .undek file can come from anywhere, so the lexer and parser are the first
// things it touches. A rejected macro is a normal outcome here; a crash or a
// read past the end of a token is what the fuzzer is looking for.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Anything past this isn't a macro anyone would save.
    if (size > 256 * 1024) return 0;

    const std::string src(reinterpret_cast<const char*>(data), size);
    try {
        undek::script::Lexer lexer(src);
        undek::script::Parser parser(lexer.tokenize());
        parser.parse();
    } catch (const std::exception&) {
        // Error messages are the expected result for mangled input.
    }
    return 0;
}
