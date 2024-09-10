// Class that calls an internal Windows API to acquire information about all handles held by all processes.

// Need to define WIN32_NO_STATUS temporarily when including both Windows.h and ntstatus.h
#define WIN32_NO_STATUS
#include <Windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <sstream>
#include <fstream>
#include "AllHandlesSystemwide.h"
#include "FileOutput.h"
#include "HEX.h"
#include "SysErrorMessage.h"

/// <summary>
/// Acquire information about the current set of handles held by all processes
/// </summary>
/// <param name="sErrorInfo">Output: Information about any failures during acquisition</param>
/// <returns>true if successful</returns>
bool AllHandlesSystemwide::Update(std::wstring& sErrorInfo)
{
    // Initialize output variable
    sErrorInfo.clear();
    // Initialize memory buffer
    Clear();

    // Get pointer to NtQuerySystemInformation API in ntdll.dll
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (nullptr == ntdll)
    {
        sErrorInfo = L"Couldn't get module ntdll.dll";
        return false;
    }
    pfn_NtQuerySystemInformation_t NtQuerySystemInformation = (pfn_NtQuerySystemInformation_t)GetProcAddress(ntdll, "NtQuerySystemInformation");
    if (nullptr == NtQuerySystemInformation)
    {
        sErrorInfo = L"Couldn't get function NtQuerySystemInformation";
        return false;
    }

    // First call: pass in the minimal-size buffer to get the required buffer size to retrieve all handle info.
    // Smaller buffer than this and returns a value that doesn't help us.
    byte dummyBuffer[sizeof(SYSTEM_HANDLE_INFORMATION_EX)] = { 0 };
    ULONG sysInfoLength = 0, returnLength = 0;
    NTSTATUS ntStat = NtQuerySystemInformation(SystemExtendedHandleInformation, &dummyBuffer, sizeof(dummyBuffer), &returnLength);
    // Problem if the API returns anything but STATUS_INFO_LENGTH_MISMATCH
    if (STATUS_INFO_LENGTH_MISMATCH != ntStat)
    {
        std::wstringstream strErrorInfo;
        strErrorInfo << L"NtQuerySystemInformation first call failed: " << SysErrorMessageWithCode(ntStat, true);
        sErrorInfo = strErrorInfo.str();
        return false;
    }

    // Repeat in a loop until successful:
    // Whatever the last API call said was necessary, allocate 25% more than that in case more handles get opened 
    // between that call and the next.
    while (STATUS_INFO_LENGTH_MISMATCH == ntStat)
    {
        // Deallocate previous allocation.
        Clear();

        // 25% higher than last demanded
        sysInfoLength = returnLength + (returnLength / 4);
        // Unlikely to overflow, but check anyway
        if (sysInfoLength < returnLength)
        {
            sErrorInfo = L"Unable to allocate memory: integer overflow";
            return false;
        }
        // Allocate the memory
        if (!m_Mem.Alloc(sysInfoLength, sErrorInfo))
        {
            return false;
        }
        // Get extended information about handles, systemwide
        ntStat = NtQuerySystemInformation(SystemExtendedHandleInformation, m_Mem.Get(), sysInfoLength, &returnLength);

        switch (ntStat)
        {
        case STATUS_SUCCESS:
            // Successful
            return true;

        case STATUS_INFO_LENGTH_MISMATCH:
            // Not enough memory - try again based on new returnLength
            break;

        default:
        {
            // Something went wrong. Fail out.
            std::wstringstream strErrorInfo;
            strErrorInfo << L"NtQuerySystemInformation second call failed: " << SysErrorMessageWithCode(ntStat, true) << std::endl
                << L"returnLength = " << returnLength << std::endl
                << L"had allocated  " << sysInfoLength;
            sErrorInfo = strErrorInfo.str();
            return false;
        }
        }
    }

    // Won't ever actually exit here, but compiler complains if this line not included :)
    return true;
}

/// <summary>
/// Returns the number of handles for which information was obtained by the last Update call.
/// </summary>
ULONG_PTR AllHandlesSystemwide::NumberOfHandles() const
{
    const PSYSTEM_HANDLE_INFORMATION_EX pHandleCollection = Get();
    if (nullptr == pHandleCollection)
        return 0;
    return pHandleCollection->NumberOfHandles;
}

/// <summary>
/// Information obtained about a specific handle (by index) by the last Update call.
/// </summary>
/// <param name="ix">Input: 0-based index, up to one less than NumberOfHandles</param>
/// <returns>Pointer to information if successful; nullptr if requested index is out of range</returns>
const PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX AllHandlesSystemwide::HandleInfo(ULONG_PTR ix) const
{
    const PSYSTEM_HANDLE_INFORMATION_EX pHandleCollection = Get();
    if (nullptr == pHandleCollection)
        return nullptr;
    if (ix >= pHandleCollection->NumberOfHandles)
        return nullptr;
    return &pHandleCollection->Handles[ix];
}

/// <summary>
/// Diagnostic dump; writes information acquired by last Update call to a tab-delimited file
/// </summary>
/// <param name="szOutFile">Input: full path to output file</param>
/// <param name="bAppend">Input: true to append to the file; false to overwrite it</param>
    /// <param name="sErrorInfo">Output: Information about any errors on failure</param>
/// <returns>true if successful</returns>
bool AllHandlesSystemwide::Dump(const wchar_t* szOutFile, bool bAppend, std::wstring& sErrorInfo) const
{
    // Output file stream, optionally appending
    std::wofstream fs;
    if (!CreateFileOutput(szOutFile, fs, bAppend))
    {
        std::wstringstream strErrorInfo;
        strErrorInfo << L"AllHandlesSystemwide::Dump to " << szOutFile << L" fails";
        sErrorInfo = strErrorInfo.str();
        return false;
    }

    // Tab-delimited headers
    fs
        << L"PID\t"
        << L"Handle\t"
        << L"ObjectTypeIndex\t"
        << L"ObjectAddr" << std::endl;

    const ULONG_PTR nHandles = NumberOfHandles();
    for (ULONG_PTR ix = 0; ix < nHandles; ++ix)
    {
        const PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX pInfo = HandleInfo(ix);
        if (nullptr != pInfo)
        {
            fs
                << pInfo->UniqueProcessId << L"\t"
                << HEX(pInfo->HandleValue, 8, false, true) << L"\t"
                << pInfo->ObjectTypeIndex << L"\t"
                << pInfo->Object << std::endl;
        }
        else
        {
            fs << L"NULL" << std::endl;
        }
    }
    
    // Close the file stream
    fs.close();

    return true;
}
