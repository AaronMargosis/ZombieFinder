// Heap memory allocation management

#pragma once

/// <summary>
/// Class to manage a large heap allocation and automatically deallocate,
/// without raising exceptions on failure
/// </summary>
class HeapMem
{
public:
    // Default ctor
    HeapMem() = default;
    // Dtor: deallocate anything that's been allocated
    virtual ~HeapMem() { Dealloc(); }

    /// <summary>
    /// Allocate a block of virtual memory from the current process heap.
    /// Deallocates any previous allocated memory.
    /// </summary>
    /// <param name="nBytes">Input: number of bytes to allocate</param>
    /// <param name="sErrorInfo">Output: information about any failure</param>
    /// <returns>true if successful</returns>
    bool Alloc(size_t nBytes, std::wstring& sErrorInfo);

    /// <summary>
    /// Deallocate any previously allocated memory
    /// </summary>
    /// <param name="sErrorInfo">Output: information about any failure</param>
    /// <returns>true if successful</returns>
    bool Dealloc(std::wstring& sErrorInfo);

    /// <summary>
    /// Deallocate any previously allocated memory. (No string output in case of failure.)
    /// </summary>
    /// <returns>true if successful</returns>
    bool Dealloc();

    /// <summary>
    /// Returns a pointer to the most recently allocated buffer
    /// </summary>
    PVOID Get() const { return m_pMem; }

    /// <summary>
    /// Returns the current allocation size
    /// </summary>
    size_t Size() const { return m_nSize; }

private:
    /// <summary>
    /// Returns a handle to the process heap, handling any error conditions
    /// Note that this is not a handle to a kernel/executive object; do not call CloseHandle on it.
    /// </summary>
    /// <param name="sErrorInfo">Output: information about any failure</param>
    /// <returns>Handle to process heap if successful; nullptr if unsuccessful</returns>
    HANDLE ProcessHeap(std::wstring& sErrorInfo);

private:
    // Pointer to memory buffer
    PVOID m_pMem = NULL;
    size_t m_nSize = 0;

private:
    // Not implemented
    HeapMem(const HeapMem&) = delete;
    HeapMem& operator = (const HeapMem&) = delete;
};
