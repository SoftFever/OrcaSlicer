#include <iostream>
#include <fstream>
#include <ios>
#include <iomanip>


int main(int argc, char const *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <file> <symbol name>" << std::endl;
        return -1;
    }

    const char* filename = argv[1];
    const char* symbol = argv[2];

    size_t size = 0;
    std::fstream file(filename);
    if (!file.good()) {
        std::cerr << "Cannot read file: " << filename << std::endl;
    }

    std::cout << "/* WARN: This file is auto-generated from `" << filename << "` */" << std::endl;
    std::cout << "const unsigned char " << symbol << "[] = {";

    char c;
    std::cout << std::hex;
    std::cout.fill('0');
    for (file.get(c); !file.eof(); size++, file.get(c)) {
        if (size % 12 == 0) { std::cout << "\n    "; }
        std::cout << "0x" << std::setw(2) << (unsigned)c << ", ";
    }

    std::cout << "\n    0, 0\n};\n";

    std::cout << std::dec;
    std::cout << "const size_t " << symbol << "_size = " << size << ";" << std::endl;
    std::cout << "const size_t " << symbol << "_size_yy = " << size + 2 << ";" << std::endl;

    return 0;
}
