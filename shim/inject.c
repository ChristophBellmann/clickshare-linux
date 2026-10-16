/*
 * inject.exe <program.exe> <dll> [delay_ms]
 * Starts <program.exe>, waits until it is up and loads <dll> into it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

int wmain(int argc, WCHAR **argv)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR dll[MAX_PATH], cwd[MAX_PATH], *p;
    DWORD delay = argc > 3 ? _wtoi(argv[3]) : 3000;
    SIZE_T len;
    void *mem;
    HANDLE thread;

    if (argc < 3)
    {
        fwprintf(stderr, L"usage: inject.exe <program.exe> <dll> [delay_ms]\n");
        return 1;
    }
    GetFullPathNameW(argv[2], MAX_PATH, dll, NULL);
    GetFullPathNameW(argv[1], MAX_PATH, cwd, NULL);
    if ((p = wcsrchr(cwd, '\\'))) *p = 0;

    if (!CreateProcessW(argv[1], NULL, NULL, NULL, FALSE, 0, NULL, cwd, &si, &pi))
    {
        fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    WaitForInputIdle(pi.hProcess, 15000);
    Sleep(delay);

    len = (wcslen(dll) + 1) * sizeof(WCHAR);
    mem = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    WriteProcessMemory(pi.hProcess, mem, dll, len, NULL);
    thread = CreateRemoteThread(pi.hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"),
            mem, 0, NULL);
    if (!thread)
    {
        fwprintf(stderr, L"CreateRemoteThread failed: %lu\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(thread, 10000);
    {
        DWORD code = 0;
        GetExitCodeThread(thread, &code);
        wprintf(L"injected %ls -> module %#lx\n", dll, code);
    }
    CloseHandle(thread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
