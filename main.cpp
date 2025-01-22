#include <mimalloc.h>

#include <cstdio>
#include <string>

#include "flinch.hpp"

int main(int argc, char ** argv)
{
    if (argc != 2)
        return puts("Usage: ./flinch <filename>"), 0;

    auto file = fopen(argv[1], "rb");
    if (!file)
        return printf("Failed to open file %s\n", argv[1]), 1;

    fseek(file, 0, SEEK_END);
    long int fsize = ftell(file);
    rewind(file);

    if (fsize < 0)
        return printf("Failed to read from file %s\n", argv[1]), 1;
    
    string text;
    text.resize(fsize);

    size_t bytes_read = fread(text.data(), 1, fsize, file);
    fclose(file);

    if (bytes_read != (size_t)fsize)
        return printf("Failed to read from file %s\n", argv[1]), 1;

    auto p = load_program(text);
    interpret(p);
}
