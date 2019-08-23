#include <iostream>
#include <fstream>
#include <ios>
#include <iomanip>


int main(int argc, char const *argv[])
{
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <file> <symbol name> <output file>" << std::endl;
        return -1;
    }

    const char* filename_in = argv[1];
    const char* symbol = argv[2];
    const char* filename_out = argv[3];

    size_t size = 0;
    std::fstream file(filename_in, std::ios::in | std::ios::binary);
    if (!file.good()) {
        std::cerr << "Cannot read file: " << filename_in << std::endl;
    }

    std::fstream output(filename_out, std::ios::out | std::ios::trunc);
    if (!output.good()) {
        std::cerr << "Cannot open output file: " << filename_out << std::endl;
    }

    output << "/* WARN: This file is auto-generated from `" << filename_in << "` */" << std::endl;
    output << "const unsigned char " << symbol << "[] = {";

    char c;
    output << std::hex;
    output.fill('0');
    for (file.get(c); !file.eof(); size++, file.get(c)) {
        if (size % 12 == 0) { output << "\n    "; }
        output << "0x" << std::setw(2) << (unsigned)c << ", ";
    }

    output << "\n    0, 0\n};\n";

    output << std::dec;
    output << "const size_t " << symbol << "_size = " << size << ";" << std::endl;
    output << "const size_t " << symbol << "_size_yy = " << size + 2 << ";" << std::endl;

    return 0;
}
