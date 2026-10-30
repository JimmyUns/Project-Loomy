// fake windows.h: just enough Win32 to run Loomy's logic on Linux
#pragma once
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
typedef void *HANDLE, *HWND, *HDC, *HFONT, *HINSTANCE, *HGDIOBJ, *HICON, *HBRUSH, *HMENU;
typedef unsigned int DWORD, UINT, COLORREF;
typedef int BOOL, LONG;
typedef uintptr_t WPARAM; typedef intptr_t LPARAM, INT_PTR, LRESULT;
typedef unsigned short WORD; typedef short SHORT; typedef unsigned long long ULONGLONG;
typedef const wchar_t *LPCWSTR;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG cx, cy; } SIZE;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct { DWORD lo, hi; } FILETIME;
typedef struct { DWORD dwFileAttributes; FILETIME a,b,ftLastWriteTime; DWORD h,l; } WIN32_FILE_ATTRIBUTE_DATA;
typedef struct { DWORD dwFileAttributes; wchar_t cFileName[260]; } WIN32_FIND_DATAW;
#define MAX_PATH 260
#define RGB(r,g,b) ((COLORREF)(((r)&255)|(((g)&255)<<8)|(((b)&255)<<16)))
#define GetRValue(c) ((c)&255)
#define GetGValue(c) (((c)>>8)&255)
#define GetBValue(c) (((c)>>16)&255)
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define CP_UTF8 65001
#define SW_SHOWNORMAL 1
#define GetFileExInfoStandard 0
#define FILE_ATTRIBUTE_DIRECTORY 16
#define FILE_ATTRIBUTE_SYSTEM 4
#define VK_SHIFT 0x10
SHORT GetKeyState(int);
enum { VK_UP=1, VK_DOWN, VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_RETURN, VK_ESCAPE, VK_LEFT, VK_RIGHT, VK_TAB, VK_BACK, VK_SPACE };
#define DT_SINGLELINE 1
#define DT_NOPREFIX 2
#define DT_END_ELLIPSIS 4
#define DT_WORDBREAK 8
#define DT_CALCRECT 16
#define WINAPI
HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, void*);
BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
DWORD GetFileSize(HANDLE, DWORD*);
BOOL CloseHandle(HANDLE);
HANDLE GetProcessHeap(void);
void *HeapAlloc(HANDLE, DWORD, size_t);
BOOL HeapFree(HANDLE, DWORD, void*);
int MultiByteToWideChar(UINT, DWORD, const char*, int, wchar_t*, int);
int WideCharToMultiByte(UINT, DWORD, const wchar_t*, int, char*, int, void*, void*);
void GetLocalTime(SYSTEMTIME*);
INT_PTR ShellExecuteW(HWND, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, int);
void Sleep(DWORD);
int wsprintfW(wchar_t*, const wchar_t*, ...);
int wvsprintfW(wchar_t*, const wchar_t*, va_list);
DWORD ExpandEnvironmentStringsW(const wchar_t*, wchar_t*, DWORD);
BOOL CreateDirectoryW(const wchar_t*, void*);
BOOL GetFileAttributesExW(const wchar_t*, int, void*);
LONG CompareFileTime(const FILETIME*, const FILETIME*);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
int DrawTextW(HDC, const wchar_t*, int, RECT*, UINT);
BOOL GetTextExtentPoint32W(HDC, const wchar_t*, int, SIZE*);
COLORREF SetTextColor(HDC, COLORREF);
HANDLE FindFirstFileW(const wchar_t*, WIN32_FIND_DATAW*);
BOOL FindNextFileW(HANDLE, WIN32_FIND_DATAW*);
BOOL FindClose(HANDLE);
DWORD GetModuleFileNameW(void*, wchar_t*, DWORD);
// display layout (Win+P)
typedef unsigned int UINT32;
typedef struct { char b[72]; } DISPLAYCONFIG_PATH_INFO;
typedef struct { char b[64]; } DISPLAYCONFIG_MODE_INFO;
typedef int DISPLAYCONFIG_TOPOLOGY_ID;
#define ERROR_SUCCESS 0
#define QDC_DATABASE_CURRENT 4
#define SDC_TOPOLOGY_INTERNAL 1
#define SDC_TOPOLOGY_CLONE 2
#define SDC_TOPOLOGY_EXTEND 4
#define SDC_TOPOLOGY_EXTERNAL 8
#define SDC_APPLY 0x80
LONG GetDisplayConfigBufferSizes(UINT32, UINT32*, UINT32*);
LONG QueryDisplayConfig(UINT32, UINT32*, DISPLAYCONFIG_PATH_INFO*, UINT32*, DISPLAYCONFIG_MODE_INFO*, DISPLAYCONFIG_TOPOLOGY_ID*);
LONG SetDisplayConfig(UINT32, void*, UINT32, void*, UINT32);
// Pocket Loomy (phone.c)
#define WM_APP 0x8000
#define MB_ERR_INVALID_CHARS 8
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define OUT_TT_PRECIS 4
#define CLIP_DEFAULT_PRECIS 0
#define CLEARTYPE_QUALITY 5
#define FIXED_PITCH 1
#define FF_MODERN 0x30
HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const wchar_t*);
BOOL DeleteObject(HGDIOBJ);
BOOL DeleteFileW(const wchar_t*);
