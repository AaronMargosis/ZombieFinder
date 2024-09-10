// Miscellaneous utility functions

#include <Windows.h>
#include <string>
#include <sstream>
#include "SysErrorMessage.h"
#include "UtilityFunctions.h"

// ----------------------------------------------------------------------------------------------------

/// <summary>
/// Gets the executable image path associated with a Process ID, if that process is running
/// </summary>
/// <param name="pid">Input: process ID</param>
/// <param name="sProcessImagePath">Output: full image path of executable, if running; empty string otherwise</param>
/// <returns>true if successful</returns>
bool GetImagePathFromPID(ULONG_PTR pid, std::wstring& sProcessImagePath)
{
    sProcessImagePath.clear();

    // Getting the executable image path of the parent process requires PROCESS_QUERY_LIMITED_INFORMATION or PROCESS_QUERY_INFORMATION
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (NULL != hProcess)
    {
        // MAX_PATH*2 should be plenty for all expected use cases.
        // Unfortunately, if QueryFullProcessImageNameW fails with ERROR_INSUFFICIENT_BUFFER, the fourth parameter does not return
        // the required buffer size, as most APIs like this do.
        wchar_t szImagePath[MAX_PATH * 2] = { 0 };
        DWORD dwPathSize = sizeof(szImagePath) / sizeof(szImagePath[0]);
        BOOL ret = QueryFullProcessImageNameW(hProcess, 0, szImagePath, &dwPathSize);
        CloseHandle(hProcess);
        if (ret)
        {
            sProcessImagePath = szImagePath;
            return true;
        }
        else
        {
            DWORD dwLastError = GetLastError();
            sProcessImagePath = SysErrorMessageWithCode(dwLastError);
            //std::wcerr << L"QueryFullProcessImageNameW failed for PID " << pid << L": " << sProcessImagePath << std::endl;
        }
    }
    else
    {
        DWORD dwLastErr = GetLastError();
        sProcessImagePath = SysErrorMessageWithCode(dwLastErr);
        //std::wcerr << L"OpenProcess failed for PID " << pid << L": " << sProcessImagePath << std::endl;
    }
    return false;

}

// ----------------------------------------------------------------------------------------------------

/// <summary>
/// Gets the executable image path of the parent process, if possible.
/// "Possible" means that the input parent process ID is a still-running process and that its start time
/// is earlier than the child process start time.
/// </summary>
/// <param name="ppid">Input: parent process ID</param>
/// <param name="ftChildStartTime">Input: child process start time</param>
/// <param name="sProcessImagePath">Output: full image path of executable associated with ppid</param>
/// <returns>true if successful</returns>
bool GetParentProcessImagePathIfStillRunning(ULONG_PTR ppid, const FILETIME& ftChildStartTime, std::wstring& sProcessImagePath)
{
    bool retval = false;
    sProcessImagePath.clear();
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(ppid));
    if (nullptr != hProcess)
    {
        // Note: FILETIME, ULARGE_INTEGER, and ULONGLONG are all 8 bytes, and lay out the same way.
        FILETIME ftCreateTime, ftExitTime, ftKernelTime, ftUserTime;
        if (GetProcessTimes(hProcess, &ftCreateTime, &ftExitTime, &ftKernelTime, &ftUserTime))
        {
            const ULONGLONG& ulStartTime = (*(const ULONGLONG*)&ftCreateTime);
            const ULONGLONG& ulChildStartTime = (*(const ULONGLONG*)&ftChildStartTime);
            if (ulStartTime < ulChildStartTime)
            {
                retval = true;

                // MAX_PATH*2 should be plenty for all expected use cases.
                // Unfortunately, if QueryFullProcessImageNameW fails with ERROR_INSUFFICIENT_BUFFER, the fourth parameter does not return
                // the required buffer size, as most APIs like this do.
                wchar_t szImagePath[MAX_PATH * 2] = { 0 };
                DWORD dwPathSize = sizeof(szImagePath) / sizeof(szImagePath[0]);
                BOOL ret = QueryFullProcessImageNameW(hProcess, 0, szImagePath, &dwPathSize);
                if (ret)
                {
                    sProcessImagePath = szImagePath;
                }
            }
        }
        CloseHandle(hProcess);
    }
    return retval;
}

// ----------------------------------------------------------------------------------------------------

/// <summary>
/// Converts the input number of total seconds into an English-language string incorporating "days", "hrs", "min" as appropriate
/// Examples:
/// Ago(90) --> "1 min 30 secs"
/// Ago(100000) --> "1 day 3 hrs 46 min 40 secs"
/// </summary>
std::wstring Ago(ULONGLONG nSecondsAgo)
{
    ULONGLONG nDays = nSecondsAgo / ULONGLONG(24 * 3600);

    nSecondsAgo = nSecondsAgo % ULONGLONG(24 * 3600);
    ULONGLONG nHours = nSecondsAgo / 3600;

    nSecondsAgo %= 3600;
    ULONGLONG nMinutes = nSecondsAgo / 60;

    nSecondsAgo %= 60;
    ULONGLONG nSeconds = nSecondsAgo;

    std::wstringstream str;
    bool bShow = false;
    if (nDays > 0)
    {
        str << nDays << (nDays == 1 ? L" day " : L" days ");
        bShow = true;
    }
    if (bShow || nHours > 0)
    {
        str << nHours << (nHours == 1 ? L" hour " : L" hrs ");
        bShow = true;
    }
    if (bShow || nMinutes > 0)
    {
        str << nMinutes << L" min ";
    }
    str << nSeconds << L" secs";
    return str.str();
}

