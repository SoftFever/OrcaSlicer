extern "C" {
#include "avrdude.h"
}


#ifdef WIN32
#include <stdlib.h>
#include <vector>

extern "C" {
#include "windows/utf8.h"
}

struct ArgvUtf8 : std::vector<char*>
{
	int argc;

	ArgvUtf8(int argc_w, wchar_t *argv_w[]) : std::vector<char*>(argc_w + 1, nullptr), argc(0)
	{
		for (int i = 0; i < argc_w; i++) {
			char *arg_utf8 = ::wstr_to_utf8(argv_w[i], -1);
			if (arg_utf8 != nullptr) {
				operator[](i) = arg_utf8;
				argc = i + 1;
			} else {
				break;
			}
		}
	}

	~ArgvUtf8()
	{
		for (char *arg : *this) {
			if (arg != nullptr) {
				::free(arg);
			}
		}
	}
};

#endif

#ifdef _MSC_VER

int wmain(int argc_w, wchar_t *argv_w[])
{
	ArgvUtf8 argv_utf8(argc_w, argv_w);
	return ::avrdude_main(argv_utf8.argc, &argv_utf8[0]);
}

#else

int main(int argc, char *argv[])
{
	return ::avrdude_main(argc, argv);
}

#endif
