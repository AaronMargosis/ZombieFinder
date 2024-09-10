// Heap memory allocation management
// Class to manage a large heap allocation and automatically deallocate,
// without raising exceptions on failure

#include <Windows.h>
#include <sstream>
#include "SysErrorMessage.h"
#include "HeapMem.h"

/// <summary>
/// Allocate a block of virtual memory from the current process heap.
/// Deallocates any previous allocated memory.
/// </summary>
/// <param name="nBytes">Input: number of bytes to allocate</param>
/// <param name="sErrorInfo">Output: information about any failure</param>
/// <returns>true if successful</returns>
bool HeapMem::Alloc(size_t nBytes, std::wstring& sErrorInfo)
{
    // Init output variable
    sErrorInfo.clear();
    // Deallocate any previously allocated memory
    if (!Dealloc(sErrorInfo))
        return false;
    // Get the process heap for the current process
    HANDLE hProcessHeap = ProcessHeap(sErrorInfo);
    if (nullptr == hProcessHeap)
        return false;
    // Allocate heap memory
    m_pMem = HeapAlloc(hProcessHeap, 0, nBytes);
    if (nullptr != m_pMem)
    {
        m_nSize = nBytes;
    }
    else
    {
        std::wstringstream strErrorInfo;
        strErrorInfo << L"HeapAlloc failed to allocate " << nBytes << L" bytes";
        sErrorInfo = strErrorInfo.str();
        return false;
    }
    return true;
}

/// <summary>
/// Deallocate any previously allocated memory
/// </summary>
/// <param name="sErrorInfo">Output: information about any failure</param>
/// <returns>true if successful</returns>
bool HeapMem::Dealloc(std::wstring& sErrorInfo)
{
    // Init output variable
    sErrorInfo.clear();
    if (nullptr != m_pMem)
    {
        // Get the process heap for the current process
        HANDLE hProcessHeap = ProcessHeap(sErrorInfo);
        if (nullptr == hProcessHeap)
            return false;
        // Deallocate the memory, clear pointer
        BOOL ret = HeapFree(hProcessHeap, 0, m_pMem);
        m_pMem = nullptr;
        m_nSize = 0;
        if (!ret)
        {
            DWORD dwLastErr = GetLastError();
            std::wstringstream strErrorInfo;
            strErrorInfo << L"HeapFree failed: " << SysErrorMessageWithCode(dwLastErr);
            sErrorInfo = strErrorInfo.str();
            return false;
        }
    }
    return true;
}

/// <summary>
/// Deallocate any previously allocated memory. (No string output in case of failure.)
/// </summary>
/// <returns>true if successful</returns>
bool HeapMem::Dealloc()
{
    std::wstring sUnused;
    return Dealloc(sUnused);
}

/// <summary>
/// Returns a handle to the process heap, handling any error conditions
/// Note that this is not a handle to a kernel/executive object; do not call CloseHandle on it.
/// </summary>
/// <param name="sErrorInfo">Output: information about any failure</param>
/// <returns>Handle to process heap if successful; nullptr if unsuccessful</returns>
HANDLE HeapMem::ProcessHeap(std::wstring& sErrorInfo)
{
    // API call to get a handle to the current process heap. 
    HANDLE hProcessHeap = GetProcessHeap();
    if (nullptr == hProcessHeap)
    {
        DWORD dwLastErr = GetLastError();
        std::wstringstream strErrorInfo;
        strErrorInfo << L"GetProcessHeap failed: " << SysErrorMessageWithCode(dwLastErr);
        sErrorInfo = strErrorInfo.str();
    }
    return hProcessHeap;
}

