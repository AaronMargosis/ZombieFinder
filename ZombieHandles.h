// Class to acquire information about and new handles to processes that have exited but are still represented in kernel memory.
// Provides option to ignore recently-exited processes. (Give handle owners a little bit of time to release handles after process exit.)

#pragma once

#include <Windows.h>
#include "NtInternal.h"
#include "ZombieProcessThreadInfo.h"

/// <summary>
/// Class to acquire information about and handles to processes that have exited but are still represented in kernel memory.
/// Also gets handles to any still-existing threads in those processes.
/// Provides option to ignore recently-exited processes. (Give handle owners a little bit of time to release handles after process exit.)
/// Fills two lookup structures: one based on handle values, and one based on PID (provided by caller).
/// </summary>
class ZombieHandles
{
public:
    // Default ctor
    ZombieHandles() = default;
    // Dtor - release handles
    virtual ~ZombieHandles() {
        ReleaseAcquiredHandles();
    }

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
    bool AcquireNewHandlesToExistingZombies(ULONGLONG nAgeInSeconds, ZombiePidLookup_t& zombiePidLookup, ProcessEnumErrorInfoList_t& processEnumErrors, std::wstring& sErrorInfo);

    /// <summary>
    /// Returns a lookup object that maps handle values in the current process to information about zombie processes/threads.
    /// </summary>
    const ZombieHandleLookup_t& ZombieHandleLookup() const { return m_ZombieHandleLookup; }

    /// <summary>
    /// Returns number of zombie processes identified.
    /// </summary>
    size_t ZombieProcessCount() const { return m_nZombieProcesses; }

    /// <summary>
    /// Total number of processes, including those that have exited.
    /// </summary>
    size_t TotalProcessCount() const { return m_nTotalProcesses; }

    /// <summary>
    /// Diagnostic dump; writes information acquired by last AcquireNewHandlesToExistingZombies call to a tab-delimited file
    /// </summary>
    /// <param name="szOutFile">Input: full path to output file</param>
    /// <param name="bAppend">Input: true to append to the file; false to overwrite it</param>
    /// <param name="sErrorInfo">Output: Information about any errors on failure</param>
    /// <returns>true if successful</returns>
    bool Dump(const wchar_t* szOutFile, bool bAppend, std::wstring& sErrorInfo) const;

private:
    /// <summary>
    /// Cleanup: release handles held in the handle-based lookup collection, and clear that collection
    /// </summary>
    void ReleaseAcquiredHandles();

private:
    ZombieHandleLookup_t m_ZombieHandleLookup;
    size_t m_nZombieProcesses = 0, m_nTotalProcesses = 0;

private:
    // Not implemented
    ZombieHandles(const ZombieHandles&) = delete;
    ZombieHandles& operator = (const ZombieHandles&) = delete;
};


