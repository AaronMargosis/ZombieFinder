// Need to define WIN32_NO_STATUS temporarily when including both Windows.h and ntstatus.h
#define WIN32_NO_STATUS
#include <Windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <iostream>
#include <sstream>
#include "NtInternal.h"
#include "HEX.h"
#include "SysErrorMessage.h"
#include "FullThreadReport.h"

/// <summary>
/// Indicates whether the process or thread has exited.
/// </summary>
/// <param name="hProcessOrThread">Input: handle to a process or a thread</param>
/// <param name="bHasExited">Output: if function is successful, true if process has exited, false otherwise; undefined if function fails</param>
/// <returns>true if function succeeds, false otherwise</returns>
static bool HasExited(HANDLE hProcessOrThread, bool& bHasExited)
{
    DWORD dwLastErr = 0;
    DWORD dwRet = WaitForSingleObject(hProcessOrThread, 0);
    switch (dwRet)
    {
    case WAIT_OBJECT_0:
        bHasExited = true;
        return true;
    case WAIT_TIMEOUT:
        bHasExited = false;
        return true;
    default:
        dwLastErr = GetLastError();
        return false;
    }
}

/// <summary>
/// Lists all process objects on the system, indicating whether each has exited, how many active and exited thread objects 
/// are associated with it, and its handle count.
/// </summary>
/// <param name="pStream">Output: stream to write report to</param>
/// <returns>true if successful, false otherwise.</returns>
bool FullThreadReport(std::wostream* pStream)
{
    // Acquire pointers to ntdll interfaces
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (nullptr == ntdll)
    {
        std::wcerr << L"Couldn't get module ntdll.dll" << std::endl;
        return false;
    }
    pfn_NtGetNextProcess_t NtGetNextProcess = (pfn_NtGetNextProcess_t)GetProcAddress(ntdll, "NtGetNextProcess");
    pfn_NtGetNextThread_t NtGetNextThread = (pfn_NtGetNextThread_t)GetProcAddress(ntdll, "NtGetNextThread");
    pfn_NtQueryInformationProcess_t NtQueryInformationProcess = (pfn_NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (NtGetNextProcess == nullptr || NtGetNextThread == nullptr || NtQueryInformationProcess == nullptr)
    {
        std::wcerr << L"ERROR: Unable to load functions from ntdll.dll" << std::endl;
        return false;
    }

    size_t nTotalProcesses = 0;

    *pStream
        << L"PID\t"
        << L"Exe image path\t"
        << L"Exited\t"
        << L"Active threads\t"
        << L"Zombie threads\t"
        << L"Total threads\t"
        << L"Handle count"
        << std::endl;

    // Use NtGetNextProcess to iterate through all processes including those that have exited.
    // Each call opens a new handle to the identified process.
    // Close handles as soon as we can - after using it to get the next process.
    // Need to use PROCESS_QUERY_LIMITED_INFORMATION for the enumeration to include protected processes and other interesting processes.
    // Using MAXIMUM_ALLOWED, or MAXIMUM_ALLOWED|PROCESS_QUERY_LIMITED_INFORMATION doesn't work. There's a never-going-to-be-fixed bug
    // in Windows where trying to open a process with MAXIMUM_ALLOWED doesn't work if PROCESS_QUERY_LIMITED_INFORMATION is the only
    // allowed permission - it needs to be requested explicitly.
    // Note that NtGetNextThread requires a process handle with PROCESS_QUERY_INFORMATION, so we'll need to open a new process
    // handle at that point.
    HANDLE hPrevProcess = nullptr, hThisProcess = nullptr;
    NTSTATUS ntGNP;
    while (STATUS_SUCCESS == (ntGNP = NtGetNextProcess(hPrevProcess, PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, 0, 0, &hThisProcess)))
    {
        // Close handles as soon as possible. Can't close hThisProcess until after we get the next process.
        if (nullptr != hPrevProcess)
        {
            CloseHandle(hPrevProcess);
        }

        // Information to gather about each process
        ULONG_PTR PID = 0;
        std::wstring sExeImagePath;
        bool bProcessHasExited = false;
        size_t nActiveThreads = 0, nExitedThreads = 0, nTotalThreads = 0;
        DWORD dwHandleCount = 0;

        nTotalProcesses++;

        // Acquire information about the process
        PROCESS_EXTENDED_BASIC_INFORMATION processExtBasicInfo = { 0 };
        processExtBasicInfo.Size = sizeof(processExtBasicInfo);
        ULONG infoLen = ULONG(sizeof(processExtBasicInfo));
#pragma warning(push)
#pragma warning(disable:6001) // False positive: "Using uninitialized memory '*hThisProcess'"
        NTSTATUS ntStat = NtQueryInformationProcess(hThisProcess, ProcessBasicInformation, &processExtBasicInfo, infoLen, &infoLen);
#pragma warning(pop)
        if (STATUS_SUCCESS != ntStat)
        {
            std::wcerr
                << L"NtQueryInformationProcess returned " << HEX(ntStat, 8, true, true) << L" during enumeration " << nTotalProcesses << std::endl
                << SysErrorMessage(ntStat, true) << std::endl;
        }
        else
        {
            PID = processExtBasicInfo.BasicInfo.UniqueProcessId;

            // Get the process' image path. Need to use NtQueryInformationProcess because Win32 API won't work for
            // a process that has exited.
            // Buffer should be large enough - add extra for the UNICODE_STRING overhead.
            byte buffer[MAX_PATH * sizeof(wchar_t) + sizeof(UNICODE_STRING)] = {0};
            ULONG returnLength = 0;
            ntStat = NtQueryInformationProcess(hThisProcess, ProcessImageFileName, buffer, MAX_PATH * sizeof(wchar_t), &returnLength);
            if (STATUS_SUCCESS == ntStat)
            {
                const wchar_t* szExeImagePath = ((UNICODE_STRING*)buffer)->Buffer;
                if (nullptr != szExeImagePath)
                    sExeImagePath = szExeImagePath;
            }
            else
            {
                sExeImagePath = SysErrorMessageWithCode(ntStat, true);
            }

            // Get the process' handle count
            GetProcessHandleCount(hThisProcess, &dwHandleCount);

            if (!HasExited(hThisProcess, bProcessHasExited))
            {
                //TODO: this shouldn't happen, but should be able to handle it if it does
                //std::wcerr << L"Unable to determine whether process has exited" << std::endl;
            }

            // Inspect each of this process' threads and count how many are still running vs. exited.
            // If we can't open the process for QueryInformation, we just won't be able to get thread counts for the process.
#pragma warning(push)
#pragma warning(disable:4244) // Nt vs. Win32 API issue: 'argument': conversion from 'ULONG_PTR' to 'DWORD', possible loss of data
            HANDLE hProcessQI = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, PID);
#pragma warning(pop)
            if (nullptr != hProcessQI)
            {
                HANDLE hPrevThread = nullptr, hThisThread = nullptr;
                NTSTATUS ntGNT;
                while (STATUS_SUCCESS == (ntGNT = NtGetNextThread(hProcessQI, hPrevThread, THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, 0, 0, &hThisThread)))
                {
                    nTotalThreads++;

                    if (nullptr != hPrevThread)
                        CloseHandle(hPrevThread);
                    bool bThreadHasExited = false;
                    if (!HasExited(hThisThread, bThreadHasExited))
                    {
                        //TODO: this shouldn't happen, but should be able to handle it if it does
                        // Total threads won't be equal to active + exited threads.
                        //std::wcerr << L"Unable to determine whether thread has exited" << std::endl;
                    }
                    else
                    {
                        if (bThreadHasExited)
                            nExitedThreads++;
                        else
                            nActiveThreads++;
                    }
                    hPrevThread = hThisThread;
                }

                if (nullptr != hPrevThread)
                    CloseHandle(hPrevThread);

                CloseHandle(hProcessQI);

                *pStream
                    << PID << L"\t"
                    << sExeImagePath << L"\t"
                    << (bProcessHasExited ? L"Yes" : L"No") << L"\t"
                    << nActiveThreads << L"\t"
                    << nExitedThreads << L"\t"
                    << nTotalThreads << L"\t"
                    << dwHandleCount
                    << std::endl;
            }
            else
            {
                *pStream
                    << PID << L"\t"
                    << sExeImagePath << L"\t"
                    << (bProcessHasExited ? L"Yes" : L"No") << L"\t"
                    << L"-" << L"\t"
                    << L"-" << L"\t"
                    << L"-" << L"\t"
                    << dwHandleCount
                    << std::endl;
            }
        }

        // For next iteration
        hPrevProcess = hThisProcess;
    }

    // Close the last process unless we saved it off
    if (nullptr != hPrevProcess)
    {
        CloseHandle(hPrevProcess);
    }

    // Report if terminating NTSTATUS value is other than STATUS_NO_MORE_ENTRIES
    if (STATUS_NO_MORE_ENTRIES != ntGNP)
    {
        std::wcerr << L"Process enumeration failed: NtGetNextProcess returned " << HEX(ntGNP, 8, true, true) << L" after " << nTotalProcesses << L" iterations" << std::endl
            << SysErrorMessage(ntGNP, true) << std::endl;
    }

    return true;
}
