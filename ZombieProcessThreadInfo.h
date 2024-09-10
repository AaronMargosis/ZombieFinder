// Information collected about zombie processes and threads

#pragma once

#include <unordered_map>
#include <list>

/// <summary>
/// Information collected about zombie processes and threads
/// </summary>
struct ZombieProcessThreadInfo
{
    /// <summary>
    /// Process ID of zombie process
    /// </summary>
    ULONG_PTR PID = 0;

    /// <summary>
    /// Non-zero Thread ID of thread in zombie process.
    /// (0 if this object represents a process and not a thread.)
    /// </summary>
    DWORD TID = 0;

    /// <summary>
    /// Executable image path of zombie process, in Object Manager namespace
    /// E.g., "\Device\HarddiskVolume3\Windows\System32\SearchProtocolHost.exe"
    /// </summary>
    std::wstring sImagePath;
    
    /// <summary>
    /// The start and exit times of the zombie process
    /// </summary>
    FILETIME createTime = { 0 }, exitTime = { 0 };
    
    /// <summary>
    /// The number of still-existing threads in the zombie process
    /// </summary>
    ULONG nThreads = 0;
    
    /// <summary>
    /// The parent process ID of the zombie process - the PID of the process that started the now-zombie process.
    /// </summary>
    ULONG_PTR ParentPID = 0;
    
    /// <summary>
    /// The executable image path of the zombie's parent process, if it is still running. Empty string if it has since exited.
    /// In Win32 notation; e.g., "C:\Windows\System32\winlogon.exe"
    /// </summary>
    std::wstring sParentImagePath;
};

// Typedefs for collections and lookups
// 
// Handle-based lookup
typedef std::unordered_map<HANDLE, ZombieProcessThreadInfo> ZombieHandleLookup_t;
// Object address-based lookup
typedef std::unordered_map<PVOID, ZombieProcessThreadInfo> ZombieObjectAddrLookup_t;
// PID-based lookup
typedef std::unordered_map<ULONG_PTR, ZombieProcessThreadInfo> ZombiePidLookup_t;
// List of ZombieProcessThreadInfo objects
typedef std::list<ZombieProcessThreadInfo> ZombieProcessThreadInfoList_t;

// List of errors during process enumeration
typedef std::list<std::wstring> ProcessEnumErrorInfoList_t;

