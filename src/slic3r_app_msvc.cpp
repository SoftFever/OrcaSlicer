// Why?
#define _WIN32_WINNT 0x0502
// The standard Windows includes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <wchar.h>
// Let the NVIDIA and AMD know we want to use their graphics card
// on a dual graphics card system.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#include <stdlib.h>
#include <stdio.h>

#include <string>
#include <vector>

typedef int (__stdcall *Slic3rMainFunc)(int argc, wchar_t **argv);

#ifdef SLIC3R_WRAPPER_NOCONSOLE
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, wchar_t *lpCmdLine, int nCmdShow)
{
	int 	  argc;
	wchar_t **argv = CommandLineToArgvW(lpCmdLine, &argc);
#else
int wmain(int argc, wchar_t **argv)
{
#endif
	wchar_t path_to_exe[MAX_PATH + 1] = { 0 };
	::GetModuleFileNameW(nullptr, path_to_exe, MAX_PATH);
	wchar_t drive[_MAX_DRIVE];
	wchar_t dir[_MAX_DIR];
	wchar_t fname[_MAX_FNAME];
	wchar_t ext[_MAX_EXT];
	_wsplitpath(path_to_exe, drive, dir, fname, ext);
	_wmakepath(path_to_exe, drive, dir, nullptr, nullptr);

// https://wiki.qt.io/Cross_compiling_Mesa_for_Windows
// http://download.qt.io/development_releases/prebuilt/llvmpipe/windows/
	wchar_t path_to_mesa[MAX_PATH + 1] = { 0 };
	wcscpy(path_to_mesa, path_to_exe);
	wcscat(path_to_mesa, L"mesa\\opengl32.dll");
	printf("Loading MESA OpenGL library: %S\n", path_to_mesa);
	HINSTANCE hInstance_OpenGL = LoadLibraryExW(path_to_mesa, nullptr, 0);
	if (hInstance_OpenGL == nullptr) {
		printf("MESA OpenGL library was not loaded\n");
	}

	wchar_t path_to_slic3r[MAX_PATH + 1] = { 0 };
//	wcscpy(path_to_slic3r, path_to_exe);
	wcscat(path_to_slic3r, L"slic3r.dll");
	printf("Loading Slic3r library: %S\n", path_to_slic3r);
	HINSTANCE hInstance_Slic3r = LoadLibraryExW(path_to_slic3r, nullptr, 0);
//	if (hInstance_OpenGL == nullptr || hInstance_Slic3r == nullptr) {
	if (hInstance_Slic3r == nullptr) {
			printf("slic3r.dll was not loaded\n");
		return -1;
	}

	// resolve function address here
	Slic3rMainFunc slic3r_main = (Slic3rMainFunc)GetProcAddress(hInstance_Slic3r, "slic3r_main");
	if (slic3r_main == nullptr) {
		printf("could not locate the function slic3r_main in slic3r.dll\n");
		return -1;
	}

	std::vector<wchar_t*> argv_extended;
	argv_extended.emplace_back(argv[0]);
#ifdef SLIC3R_WRAPPER_GUI
	std::wstring cmd_gui = L"--gui";
	argv_extended.emplace_back(const_cast<wchar_t*>(cmd_gui.data()));
#endif
	for (int i = 1; i < argc; ++ i)
		argv_extended.emplace_back(argv[i]);
	argv_extended.emplace_back(nullptr);
	return slic3r_main(argc, argv_extended.data());
}
