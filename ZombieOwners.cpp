// Class to identify zombie processes and the processes holding handles to those processes and/or their threads,
// and zombie processes for which no process has an open handle. Typically: HandleCount = 0, PointerCount > 0.
// Not yet worked out how to identify what holds those pointers.

#include <sstream>
#include <algorithm>
#include "UtilityFunctions.h"
#include "StringUtils.h"
#include "SysErrorMessage.h"
#include "SecurityUtils.h"
#include "ZombieHandles.h"
#include "AllHandlesSystemwide.h"
#include "ZombieOwners.h"

/// <summary>
/// Comparator that sorts descending by handle count, then ascending by exe name and PID.
/// </summary>
/// <param name="a"></param>
/// <param name="b"></param>
/// <returns></returns>
static bool ZombieOwnerComparator(const ZombieOwner_t* pA, const ZombieOwner_t* pB)
{
    // If the handle counts are the same...
    if (pA->zombieOwningInfo.size() == pB->zombieOwningInfo.size())
    {
        // Case-insensitive comparison of exe name
        int cmpResult = _wcsicmp(pA->sExeName.c_str(), pB->sExeName.c_str());
        // If names are the same, then sort ascending by PID
        if (0 == cmpResult)
            return pA->PID < pB->PID;
        else
            return cmpResult < 0;
    }
    else
    {
        // Sort descending by handle count
        return pA->zombieOwningInfo.size() > pB->zombieOwningInfo.size();
    }
}

/// <summary>
/// Update information about zombies and their owners, if any.
/// </summary>
/// <param name="nAgeInSeconds">Input: ignore processes that exited less than nAgeInSeconds ago.</param>
/// <param name="sErrorInfo">Output: information about any failures</param>
/// <returns>true if successful</returns>
bool ZombieOwners::Update(ULONGLONG nAgeInSeconds, const std::wstring& sDiagDirectory, std::wstring& sErrorInfo)
{
    // The work is done in Update_Impl.
    // This function exists to enable the Debug Programs privilege for the current thread
    // and to ensure that we properly revert to previous state before returning in all cases.
    // (ImpersonateSelf, AdjustTokenPrivileges, RevertToSelf.)

    // Essentially, ensure that this thread has its own token and not that of the process token.
    if (!ImpersonateSelf(SecurityImpersonation))
    {
        std::wstringstream strErrorInfo;
        DWORD dwLastErr = GetLastError();
        strErrorInfo << L"ImpersonateSelf failed: " << SysErrorMessageWithCode(dwLastErr);
        sErrorInfo = strErrorInfo.str();
        return false;
    }

    // Enable the Debug Programs privilege. Fail if it's not available to be enabled.
    std::wstring sPrivError;
    if (!EnablePrivilege(SE_DEBUG_NAME, sPrivError))
    {
        std::wstringstream strErrorInfo;
        strErrorInfo 
            << L"Cannot enable Debug Programs privilege. This program must be executed with administrative privileges." << std::endl
            << sPrivError;
        sErrorInfo = strErrorInfo.str();
        return false;
    }

    // Do the work
    bool retval = Update_Impl(nAgeInSeconds, sDiagDirectory, sErrorInfo);

    // Revert to using process token.
    RevertToSelf();

    return retval;
}

/// <summary>
/// Update information about zombies and their owners, if any.
/// </summary>
/// <param name="nAgeInSeconds">Input: ignore processes that exited less than nAgeInSeconds ago.</param>
/// <param name="sErrorInfo">Output: information about any failures</param>
/// <returns>true if successful</returns>
bool ZombieOwners::Update_Impl(ULONGLONG nAgeInSeconds, const std::wstring& sDiagDirectory, std::wstring& sErrorInfo)
{
    // Init output variable
    sErrorInfo.clear();
    // Init internal state
    m_owners.clear();
    m_unexplained.clear();
    m_nZombieProcessesAndThreads = m_nZombieProcesses = m_nTotalProcesses = 0;

    // Acquire new handles in this process to existing zombie processes and any threads they still have.
    // Also get a PID-based lookup so that we can identify zombie processes to which no process holds a handle.
    ZombieHandles zombieHandles;
    ZombiePidLookup_t zombiePidLookup;
    if (!zombieHandles.AcquireNewHandlesToExistingZombies(nAgeInSeconds, zombiePidLookup, m_processEnumErrors, sErrorInfo))
    {
        // On failure, sErrorInfo will already have been set.
        return false;
    }

    // Get counts of zombie handles and processes, and total processes
    m_nZombieProcessesAndThreads = zombieHandles.ZombieHandleLookup().size();
    m_nZombieProcesses = zombieHandles.ZombieProcessCount();
    m_nTotalProcesses = zombieHandles.TotalProcessCount();

    // Get information about all handles held by all processes.
    AllHandlesSystemwide allHandlesSystemwide;
    if (!allHandlesSystemwide.Update(sErrorInfo))
    {
        // On failure, sErrorInfo will already have been set.
        return false;
    }

    // Create an object address lookup to map kernel object addresses of zombie process/thread objects to information about those processes/threads.
    ZombieObjectAddrLookup_t zombieObjectAddrLookup;
    const ZombieHandleLookup_t& zombieHandleLookup = zombieHandles.ZombieHandleLookup();

    // Identify the process/thread handles in the current process created by the ZombieHandles instance:
    DWORD dwCurrPID = GetCurrentProcessId();
    const ULONG_PTR numHandles = allHandlesSystemwide.NumberOfHandles();
    // Iterate through all handles...
    for (ULONG_PTR ix = 0; ix < numHandles; ++ix)
    {
        PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX pHandleInfo = allHandlesSystemwide.HandleInfo(ix);
        // (Safety: but this check should never fail)
        if (nullptr != pHandleInfo)
        {
            // ... and look at handles belonging to the current process...
            if (pHandleInfo->UniqueProcessId == dwCurrPID)
            {
                // ... and specifically for the handles to the zombie processes/threads we acquired
                ZombieHandleLookup_t::const_iterator iZombie = zombieHandleLookup.find(HANDLE(pHandleInfo->HandleValue));
                if (iZombie != zombieHandleLookup.end())
                {
                    // If found, map the corresponding kernel object address to the information we collected about the process/thread.
                    zombieObjectAddrLookup[pHandleInfo->Object] = iZombie->second;
                }
            }
        }
    }

    // Now look for other processes' handles to those zombie objects.
    // Iterate through all handles...
    for (ULONG_PTR ix = 0; ix < numHandles; ++ix)
    {
        PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX pHandleInfo = allHandlesSystemwide.HandleInfo(ix);
        // (Safety: but this check should never fail)
        if (nullptr != pHandleInfo)
        {
            // ... and identify whether the handle points to one of the zombie objects ...
            ZombieObjectAddrLookup_t::const_iterator iZombie = zombieObjectAddrLookup.find(pHandleInfo->Object);
            if (iZombie != zombieObjectAddrLookup.end())
            {
                // Get information about the handle owner unless it's one that was created by the ZombieHandles instance in this process...
                // Not just ignoring ALL handles in this process - want to know if something else in this process is responsible for zombies.
                if (
                    // If the handle doesn't belong to the current process, or
                    pHandleInfo->UniqueProcessId != dwCurrPID || 
                    // It belongs to the current process but isn't one of the ones we created in the ZombieHandles instance,
                    // then keep it.
                    zombieHandleLookup.find(HANDLE(pHandleInfo->HandleValue)) == zombieHandleLookup.end())
                {
                    // The owning process' PID
                    ULONG_PTR pid = pHandleInfo->UniqueProcessId;
                    // Have we added this PID to the set yet?
                    ZombieOwnersCollection_t::iterator iterOwners = m_owners.find(pid);
                    // If not, create a new entry in the m_owners collection.
                    if (m_owners.end() == iterOwners)
                    {
                        ZombieOwner_t owner = { 0 };
                        owner.PID = pid;
                        // Get the full executable image path and exe name of the owning process
                        GetImagePathFromPID(pid, owner.sProcessImagePath);
                        owner.sExeName = GetFileNameFromFilePath(owner.sProcessImagePath);
                        // If it's a service process, get info about the hosted service(s)
                        LookupServicesByPID(pid, &owner.pServiceList);
                        // Add it to the collection
                        m_owners[pid] = owner;
                    }

                    // Add information about this handle and the corresponding zombie process/thread to the owning process' entry in m_owners.
                    ZombieOwningInfo owningInfo = { 0 };
                    owningInfo.handleValue = pHandleInfo->HandleValue;
                    owningInfo.zombieInfo = iZombie->second;
                    m_owners[pid].zombieOwningInfo.push_back(owningInfo);

                    // Remove this PID from the collection of zombies we don't have handles for.
                    ZombiePidLookup_t::iterator iPID = zombiePidLookup.find(iZombie->second.PID);
                    if (zombiePidLookup.end() != iPID)
                    {
                        zombiePidLookup.erase(iZombie->second.PID);
                    }
                }
            }
        }
    }

    // Populate the sorted collection
    for (
        ZombieOwnersCollection_t::const_iterator iter = m_owners.begin();
        iter != m_owners.end();
        iter++
        )
    {
        const ZombieOwner_t* pOwner = &(iter->second);
        m_ownersSorted.push_back(pOwner);
    }
    std::sort(m_ownersSorted.begin(), m_ownersSorted.end(), &ZombieOwnerComparator);

    // Populate the m_unexplained collection with information about zombie processes we found no handles for.
    if (zombiePidLookup.size() > 0)
    {
        for (
            ZombiePidLookup_t::const_iterator iter = zombiePidLookup.begin();
            zombiePidLookup.end() != iter;
            ++iter
            )
        {
            m_unexplained.push_back(iter->second);
        }
    }

    // Diagnostic data-dump option
    if (sDiagDirectory.size() > 0)
    {
        // Get timestamp as string
        FILETIME ft;
        SYSTEMTIME st;
        GetSystemTimeAsFileTime(&ft);
        FileTimeToSystemTime(&ft, &st);
        wchar_t szTimestamp[32];
        swprintf(szTimestamp, sizeof(szTimestamp) / sizeof(szTimestamp[0]), L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstringstream strZH, strAH, strSV;
        strZH << sDiagDirectory << L"\\ZombieFinder_" << szTimestamp << L"_ZombieHandles.txt";
        strAH << sDiagDirectory << L"\\ZombieFinder_" << szTimestamp << L"_AllHandles.txt";
        strSV << sDiagDirectory << L"\\ZombieFinder_" << szTimestamp << L"_Services.txt";

        zombieHandles.Dump(strZH.str().c_str(), false, sErrorInfo);
        allHandlesSystemwide.Dump(strAH.str().c_str(), false, sErrorInfo);
        DumpPIDtoServiceLookupInfo(strSV.str().c_str(), false, sErrorInfo);
    }

    return true;
}
