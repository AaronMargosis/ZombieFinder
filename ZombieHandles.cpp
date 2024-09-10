// Class to acquire information about and new handles to processes that have exited but are still represented in kernel memory.
// Provides option to ignore recently-exited processes. (Give handle owners a little bit of time to release handles after process exit.)

// Need to define WIN32_NO_STATUS temporarily when including both Windows.h and ntstatus.h
#define WIN32_NO_STATUS
#include <Windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <sstream>
#include <fstream>
#include "HEX.h"
#include "SysErrorMessage.h"
#include "UtilityFunctions.h"
#include "FileOutput.h"
#include "StringUtils.h"
#include "ZombieHandles.h"

/// <summary>
/// Identify and acquire handles to processes still represented in kernel memory that exited more than nAgeInSeconds ago,
/// as well as to any still-existing threads in those processes, and get information about those processes.
/// Fills in a handle-based lookup collection, and a PID-based lookup collection provided by the caller.
/// </summary>
/// <param name="nAgeInSeconds">Input: minimum number of seconds ago that a process has exited to capture its information.</param>
/// <param name="zombiePidLookup">Output: lookup structure based on PID (that caller can modify as needed)</param>
/// <param name="processEnumErrors">Output: information about any problems during process enumeration (separate from complete failure)</param>
/// <param name="sErrorInfo">Output: information about any failures</param>
/// <returns>true if successful</returns>
bool ZombieHandles::AcquireNewHandlesToExistingZombies(ULONGLONG nAgeInSeconds, ZombiePidLookup_t& zombiePidLookup, ProcessEnumErrorInfoList_t& processEnumErrors, std::wstring& sErrorInfo)
{
    // Initialize output variables
    zombiePidLookup.clear();
    processEnumErrors.clear();
    sErrorInfo.clear();
    // Initialize internal data
    m_nZombieProcesses = 0;
    m_nTotalProcesses = 0;
    ReleaseAcquiredHandles();

    // Acquire pointers to ntdll interfaces
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (nullptr == ntdll)
    {
        sErrorInfo = L"Couldn't get module ntdll.dll";
        return false;
    }
    pfn_NtGetNextProcess_t NtGetNextProcess = (pfn_NtGetNextProcess_t)GetProcAddress(ntdll, "NtGetNextProcess");
    pfn_NtGetNextThread_t NtGetNextThread = (pfn_NtGetNextThread_t)GetProcAddress(ntdll, "NtGetNextThread");
    pfn_NtQueryInformationProcess_t NtQueryInformationProcess = (pfn_NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (NtGetNextProcess == nullptr || NtGetNextThread == nullptr || NtQueryInformationProcess == nullptr)
    {
        sErrorInfo = L"ERROR: Unable to load functions from ntdll.dll";
        return false;
    }

    // Get the current time (used to determine how long ago each process exited.
    // Note that FILETIME and ULONGLONG are somewhat interchangeable here.
    ULONGLONG ulNow = 0;
    GetSystemTimeAsFileTime((LPFILETIME)&ulNow);

    // Use NtGetNextProcess to iterate through all processes including those that have exited.
    // Each call opens a new handle to the identified process.
    // Close handles that we don't need as soon as we can - after using it to get the next process.
    // Need to use PROCESS_QUERY_LIMITED_INFORMATION for the enumeration to include protected processes and other interesting processes.
    // Using MAXIMUM_ALLOWED, or MAXIMUM_ALLOWED|PROCESS_QUERY_LIMITED_INFORMATION doesn't work. There's a never-going-to-be-fixed bug
    // in Windows where trying to open a process with MAXIMUM_ALLOWED doesn't work if PROCESS_QUERY_LIMITED_INFORMATION is the only
    // allowed permission - it needs to be requested explicitly.
    // Note that NtGetNextThread requires a process handle with PROCESS_QUERY_INFORMATION, so we'll need to open a new process
    // handle at that point.
    HANDLE hPrevProcess = nullptr, hThisProcess = nullptr;
    bool bClosePrevProcess = false;
    NTSTATUS ntGNP;
    while (STATUS_SUCCESS == (ntGNP = NtGetNextProcess(hPrevProcess, PROCESS_QUERY_LIMITED_INFORMATION, 0, 0, &hThisProcess)))
    {
        // Close handles that we don't need to hold as soon as we can - we might otherwise end up with a ton of open handles.
        // Can't close the hThisProcess handle until after we get the next process.
        if (bClosePrevProcess && nullptr != hPrevProcess)
        {
            CloseHandle(hPrevProcess);
        }

        m_nTotalProcesses++;

        // Determine whether the process has exited and did so more than nAgeInSeconds ago.
        // If so, acquire information about that process
        PROCESS_EXTENDED_BASIC_INFORMATION processExtBasicInfo = { 0 };
        processExtBasicInfo.Size = sizeof(processExtBasicInfo);
        ULONG infoLen = ULONG(sizeof(processExtBasicInfo));
        bClosePrevProcess = true;
#pragma warning(push)
#pragma warning(disable:6001) // False positive: "Using uninitialized memory '*hThisProcess'"
        NTSTATUS ntStat = NtQueryInformationProcess(hThisProcess, ProcessBasicInformation, &processExtBasicInfo, infoLen, &infoLen);
#pragma warning(pop)
        if (STATUS_SUCCESS != ntStat)
        {
            std::wstringstream strErr;
            strErr << L"NtQueryInformationProcess failed during enumeration " << m_nTotalProcesses << L": " << SysErrorMessageWithCode(ntStat, true);
            processEnumErrors.push_back(strErr.str());
        }
        else
        {
            //TODO: See whether there are processes with non-zero exit times where IsProcessDeleting is not set.
            // The IsProcessDeleting flag is supposed to have been set when the process has exited.
            // If it's not set then we don't care about this process.
            if (processExtBasicInfo.IsProcessDeleting)
            {
                ZombieProcessThreadInfo zombieInfo = { 0 };

                // Get process exit time: 
                // * verify that the process has in fact exited (I've seen instances where IsProcessDeleting is set but the process is still running)
                // * ignore processes with very recent exit times - give handle holders a chance to release handles after process exit
                FILETIME unused1, unused2;
                GetProcessTimes(hThisProcess, &zombieInfo.createTime, &zombieInfo.exitTime, &unused1, &unused2);

                // View the exit time as a ULONGLONG. It will be 0 if the process has not exited.
                // Note: FILETIME, ULARGE_INTEGER, and ULONGLONG are all 8 bytes, and lay out the same way.
                //TODO: "If the process has not exited, the content of this structure is undefined." Use WaitForSingleObject to determine whether exited
                const ULONGLONG& ulExitTime = (*(const ULONGLONG*)&zombieInfo.exitTime);
                if (0 != ulExitTime)
                {
                    if (
                        // if 0, don't filter any out
                        (0 == nAgeInSeconds) ||
                        // Otherwise, ensure that exit time is more than nAgeInSeconds seconds ago.
                        (ulNow > ulExitTime && ((ulNow - ulExitTime) / 10000000) >= nAgeInSeconds)
                        )
                    {
                        // It's a zombie
                        m_nZombieProcesses++;

                        // Process ID and Parent Process ID
                        zombieInfo.PID = processExtBasicInfo.BasicInfo.UniqueProcessId;
                        zombieInfo.ParentPID = processExtBasicInfo.BasicInfo.InheritedFromUniqueProcessId;

                        // Get the parent image path if it's still running
                        GetParentProcessImagePathIfStillRunning(zombieInfo.ParentPID, zombieInfo.createTime, zombieInfo.sParentImagePath);

                        // Get the zombie process' image path. Need to use NtQueryInformationProcess because Win32 API won't work for
                        // a process that has exited.
                        // Buffer should be large enough - add extra for the UNICODE_STRING overhead.
                        byte buffer[MAX_PATH * 2 + sizeof(UNICODE_STRING)] = { 0 };
                        ULONG returnLength = 0;
                        ntStat = NtQueryInformationProcess(hThisProcess, ProcessImageFileName, buffer, MAX_PATH * 2, &returnLength);
                        if (STATUS_SUCCESS == ntStat)
                        {
                            zombieInfo.sImagePath = ((UNICODE_STRING*)buffer)->Buffer;
                        }

                        // If this process still has any existing threads, get handles to those threads and add them to the lookup.
                        // Note that we don't need to close any of these handles during this loop because we're adding all of them
                        // to our collection.
                        // If we can't open the process for QueryInformation, we just won't be able to get that thread information.
                        ULONG nThreads = 0;
#pragma warning(push)
#pragma warning(disable:4244) // Nt vs. Win32 API issue: 'argument': conversion from 'ULONG_PTR' to 'DWORD', possible loss of data
                        HANDLE hProcessQI = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, zombieInfo.PID);
#pragma warning(pop)
                        if (nullptr != hProcessQI)
                        {
                            HANDLE hThread = nullptr;
                            NTSTATUS ntGNT;
                            while (STATUS_SUCCESS == (ntGNT = NtGetNextThread(hProcessQI, hThread, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &hThread)))
                            {
                                nThreads++;
                                zombieInfo.TID = GetThreadId(hThread);
                                m_ZombieHandleLookup[hThread] = zombieInfo;
                            }

                            CloseHandle(hProcessQI);

                            //{
                            //    std::wstringstream sDebug;
                            //    sDebug << L"NtGetNextThread for PID " << zombieInfo.PID << L" terminated with " << HEX(ntGNT) << std::endl;
                            //    OutputDebugStringW(sDebug.str().c_str());
                            //}
                        }

                        // Add the process handle and the process info to the lookup object.
                        zombieInfo.TID = 0;
                        zombieInfo.nThreads = nThreads;
                        m_ZombieHandleLookup[hThisProcess] = zombieInfo;
                        zombiePidLookup[zombieInfo.PID] = zombieInfo;
                        // Do not close the current process handle on next loop through.
                        bClosePrevProcess = false;
                    }
                }
                else
                {
                    // Diagnostics; not particularly needed. If uncommented, should go to sErrorInfo, not to stderr.
                    // std::wcerr << L"IsProcessDeleting is set but there's no exit time: PID " << processExtBasicInfo.BasicInfo.UniqueProcessId << std::endl; // << L" " << zombieInfo.sImagePath << std::endl;
                }
            }
        }

        // For next iteration
        hPrevProcess = hThisProcess;
    }

    // Close the last process unless we saved it off
    if (bClosePrevProcess && nullptr != hPrevProcess)
    {
        CloseHandle(hPrevProcess);
    }

    // Report if terminating NTSTATUS value is other than 0x8000001a STATUS_NO_MORE_ENTRIES
    if (STATUS_NO_MORE_ENTRIES != ntGNP)
    {
        std::wstringstream strErr;
        strErr << L"Process enumeration failed: NtGetNextProcess returned " << HEX(ntGNP, 8, true, true) << L" after " << m_nTotalProcesses << L" iterations: " << SysErrorMessage(ntGNP, true);
        processEnumErrors.push_back(strErr.str());
    }

    return true;
}

/// <summary>
/// Cleanup: release handles held in the handle-based lookup collection, and clear that collection
/// </summary>
void ZombieHandles::ReleaseAcquiredHandles()
{
    for (
        ZombieHandleLookup_t::const_iterator iter = m_ZombieHandleLookup.begin();
        iter != m_ZombieHandleLookup.end();
        ++iter
        )
    {
        CloseHandle(iter->first);
    }
    m_ZombieHandleLookup.clear();
}

/// <summary>
/// Diagnostic dump; writes information acquired by last AcquireNewHandlesToExistingZombies call to a tab-delimited file
/// </summary>
/// <param name="szOutFile">Input: full path to output file</param>
/// <param name="bAppend">Input: true to append to the file; false to overwrite it</param>
/// <param name="sErrorInfo">Output: Information about any errors on failure</param>
/// <returns>true if successful</returns>
bool ZombieHandles::Dump(const wchar_t* szOutFile, bool bAppend, std::wstring& sErrorInfo) const
{
    // Output file stream, optionally appending
    std::wofstream fs;
    if (!CreateFileOutput(szOutFile, fs, bAppend))
    {
        std::wstringstream strErrorInfo;
        strErrorInfo << L"ZombieHandles::Dump to " << szOutFile << L" fails";
        sErrorInfo = strErrorInfo.str();
        return false;
    }
    
    // Tab-delimited headers
    fs
        << L"ThisPID\t"
        << L"HandleValue\t"
        << L"PID\t"
        << L"TID\t"
        << L"nThreads\t"
        << L"ImagePath\t"
        << L"createTime\t"
        << L"exitTime\t" 
        << L"PPID\t"
        << L"ParentImagePath"
        << std::endl;

    DWORD dwThisPID = GetCurrentProcessId();
    for (
        ZombieHandleLookup_t::const_iterator iter = m_ZombieHandleLookup.begin();
        iter != m_ZombieHandleLookup.end();
        ++iter
        )
    {
        const ZombieProcessThreadInfo& z = iter->second;
        fs
            << dwThisPID << L"\t"
            << HEX(ULONG_PTR(iter->first), 8, false, true) << L"\t"
            << z.PID << L"\t"
            << z.TID << L"\t"
            << z.nThreads << L"\t"
            << z.sImagePath << L"\t"
            << FileTimeToWString(z.createTime, false) << L"\t"
            << FileTimeToWString(z.exitTime, false) << L"\t"
            << z.ParentPID << L"\t"
            << z.sParentImagePath
            << std::endl;
    }
    fs.close();

    return true;
}
