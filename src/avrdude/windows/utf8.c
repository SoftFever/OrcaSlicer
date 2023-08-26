#include "utf8.h"


char* wstr_to_utf8(LPWSTR wstr, int len)
{
	char *res = NULL;

	int res_size = WideCharToMultiByte(CP_UTF8, 0, wstr, len, NULL, 0, NULL, NULL);
	if (res_size > 0) {
		// Note: WideCharToMultiByte doesn't null-terminate if real (ie. > 0) buffer length is passed
		res = malloc(len != - 1 ? res_size + 1 : res_size);
		if (res == NULL) { return NULL; }

		if (WideCharToMultiByte(CP_UTF8, 0, wstr, len, res, res_size, NULL, NULL) == res_size) {
			if (len != -1) { res[res_size] = '\0'; }
		} else {
			free(res);
			return NULL;
		}
	}

	return res;
}

LPWSTR utf8_to_wstr(const char *str, int len)
{
	LPWSTR res = NULL;

	int res_size = MultiByteToWideChar(CP_UTF8, 0, str, len, NULL, 0);
	if (res_size > 0) {
		// Note: MultiByteToWideChar doesn't null-terminate if real (ie. > 0) buffer length is passed
		res = malloc(len != - 1 ? res_size + 1 : res_size);

		if (res == NULL) { return NULL; }

		if (MultiByteToWideChar(CP_UTF8, 0, str, len, res, res_size) == res_size) {
			if (len != -1) { res[res_size] = L'\0'; }
		} else {
			free(res);
			return NULL;
		}
	}

	return res;
}
