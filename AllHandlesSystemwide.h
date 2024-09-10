// Class that calls an internal Windows API to acquire information about all handles held by all processes.

#pragma once

#include <string>
#include "NtInternal.h"
#include "HeapMem.h"

/// <summary>
/// A class for acquiring information all the handles held by all processes.
/// </summary>
class AllHandlesSystemwide
{
public:
    // Default ctor and dtor
    AllHandlesSystemwide() = default;
    virtual ~AllHandlesSystemwide() = default;

    /// <summary>
    /// Acquire information about the current set of handles held by all processes
    /// </summary>
    /// <param name="sErrorInfo">Output: Information about any failures during acquisition</param>
    /// <returns>true if successful</returns>
    bool Update(std::wstring& sErrorInfo);

    /// <summary>
    /// Returns the number of handles for which information was obtained by the last Update call.
    /// </summary>
    ULONG_PTR NumberOfHandles() const;

    /// <summary>
    /// Information obtained about a specific handle (by index) by the last Update call.
    /// </summary>
    /// <param name="ix">Input: 0-based index, up to one less than NumberOfHandles</param>
    /// <returns>Pointer to information if successful; nullptr if requested index is out of range</returns>
    const PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX HandleInfo(ULONG_PTR ix) const;

    /// <summary>
    /// Diagnostic dump; writes information acquired by last Update call to a tab-delimited file
    /// </summary>
    /// <param name="szOutFile">Input: full path to output file</param>
    /// <param name="bAppend">Input: true to append to the file; false to overwrite it</param>
    /// <param name="sErrorInfo">Output: Information about any errors on failure</param>
    /// <returns>true if successful</returns>
    bool Dump(const wchar_t* szOutFile, bool bAppend, std::wstring& sErrorInfo) const;

private:
    /// <summary>
    /// Clear the allocated memory structure
    /// </summary>
    void Clear() { m_Mem.Dealloc(); }

    /// <summary>
    /// Get the base address of the allocated memory structure
    /// </summary>
    /// <returns>Pointer to the allocated memory structure, or nullptr if not allocated</returns>
    const PSYSTEM_HANDLE_INFORMATION_EX Get() const { return (PSYSTEM_HANDLE_INFORMATION_EX)m_Mem.Get(); }

    /// <summary>
    /// Object to manage potentially large amount of virtual memory to acquire information.
    /// </summary>
    HeapMem m_Mem;

private:
    // Not implemented
    AllHandlesSystemwide(const AllHandlesSystemwide&) = delete;
    AllHandlesSystemwide& operator = (const AllHandlesSystemwide&) = delete;
};
