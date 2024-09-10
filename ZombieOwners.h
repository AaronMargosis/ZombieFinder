// Class to identify zombie processes and the processes holding handles to those processes and/or their threads,
// and zombie processes for which no process has an open handle. Typically: HandleCount = 0, PointerCount > 0.
// Not yet worked out how to identify what holds those pointers.

#pragma once

#include "ZombieProcessThreadInfo.h"
#include "ServiceLookupByPID.h"

/// <summary>
/// Structure combining a handle value and its corresponding process or thread.
/// </summary>
struct ZombieOwningInfo
{
    ULONG_PTR handleValue = 0;
    ZombieProcessThreadInfo zombieInfo;
};
/// <summary>
/// List of handle values and corresponding processes/threads
/// </summary>
typedef std::list<ZombieOwningInfo> ZombieOwningInfoList_t;

/// <summary>
/// Structure identifying an existing process (PID and path) that retains handles to processes that have exited
/// and/or threads within those processes.
/// </summary>
struct ZombieOwner_t
{
    ULONG_PTR PID = 0;
    std::wstring sProcessImagePath;
    std::wstring sExeName;
    const ServiceList_t* pServiceList = nullptr;
    ZombieOwningInfoList_t zombieOwningInfo;
};
/// <summary>
/// Collection of processes (uniquely identified by PID) that retain handles to zombies.
/// </summary>
typedef std::unordered_map<ULONG_PTR, ZombieOwner_t> ZombieOwnersCollection_t;

/// <summary>
/// Collection of processes that retain handles to zombies; will be sorted in descending order by handle count, then ascending by exe name.
/// </summary>
typedef std::vector<const ZombieOwner_t*> ZombieOwnersCollectionSorted_t;

/// <summary>
/// Class to identify zombie processes and the processes holding handles to those processes and/or their threads,
/// and zombie processes for which no process has an open handle. Typically: HandleCount = 0, PointerCount > 0.
/// Not yet worked out how to identify what holds those pointers.
/// </summary>
class ZombieOwners
{
public:
    // Default ctor and dtor
    ZombieOwners() = default;
    virtual ~ZombieOwners() = default;

    /// <summary>
    /// Update information about zombies and their owners, if any.
    /// </summary>
    /// <param name="nAgeInSeconds">Input: ignore processes that exited less than nAgeInSeconds ago.</param>
    /// <param name="sErrorInfo">Output: information about any failures</param>
    /// <returns>true if successful</returns>
    bool Update(ULONGLONG nAgeInSeconds, const std::wstring& sDiagDirectory, std::wstring& sErrorInfo);

    /// <summary>
    /// Returns information from most recent Update call about processes holding handles to exited processes and/or their threads.
    /// </summary>
    const ZombieOwnersCollection_t& OwnersCollection() const { return m_owners; }

    /// <summary>
    /// Returns information from most recent Update call, sorted in descending order by handle count, then ascending by exe name.
    /// </summary>
    /// <returns></returns>
    const ZombieOwnersCollectionSorted_t& OwnersCollectionSorted() const { return m_ownersSorted; }

    /// <summary>
    /// Returns information from most recent Update call about zombie processes to which no process holds an open handle.
    /// </summary>
    const ZombieProcessThreadInfoList_t& UnexplainedZombies() const { return m_unexplained; }

    /// <summary>
    /// Returns information about any errors that occurred during process enumeration.
    /// </summary>
    const ProcessEnumErrorInfoList_t& ProcessEnumErrors() const { return m_processEnumErrors; }

    /// <summary>
    /// Total number of process that have exited (and their threads) that have exited but are still represented in kernel memory.
    /// </summary>
    size_t ZombieProcessAndThreadCount() const { return m_nZombieProcessesAndThreads; }

    /// <summary>
    /// Number of zombie processes found - processes that are still represented in kernel but that exited more than nAgeInSeconds ago.
    /// </summary>
    size_t ZombieProcessCount() const { return m_nZombieProcesses; }

    /// <summary>
    /// Total number of processes that were inspected.
    /// </summary>
    size_t TotalProcessCount() const { return m_nTotalProcesses; }

private:
    /// <summary>
    /// Internal implementation for ZombieOwners::Update
    /// </summary>
    bool Update_Impl(ULONGLONG nAgeInSeconds, const std::wstring& sDiagDirectory, std::wstring& sErrorInfo);

private:
    /// <summary>
    /// Collection of information about existing processes and the handles they're holding to processes/threads that have exited.
    /// </summary>
    ZombieOwnersCollection_t m_owners;

    /// <summary>
    /// Collection of information about existing processes and the handles they're holding, sorted in descending order by handle count, then ascending by exe name.
    /// </summary>
    ZombieOwnersCollectionSorted_t m_ownersSorted;

    /// <summary>
    /// List of zombie processes for which no process holds a process or thread handle.
    /// These appear to have HandleCount = 0 and PointerCount > 0.
    /// Not yet worked out how to identify what holds those pointers.
    /// </summary>
    ZombieProcessThreadInfoList_t m_unexplained;

    /// <summary>
    /// Errors that occur during process enumeration
    /// </summary>
    ProcessEnumErrorInfoList_t m_processEnumErrors;

    // Counts
    size_t m_nZombieProcessesAndThreads = 0;
    size_t m_nZombieProcesses = 0;
    size_t m_nTotalProcesses = 0;

private:
    // Not implemented
    ZombieOwners(const ZombieOwners&) = delete;
    ZombieOwners& operator = (const ZombieOwners&) = delete;
};


