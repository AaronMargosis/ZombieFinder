// Declarations for Windows interfaces and structures beyond what's in the SDK headers.
// Some of these are documented but not all.

#pragma once

#include <winternl.h>

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX
{
    PVOID 	Object;
    ULONG_PTR 	UniqueProcessId;
    ULONG_PTR 	HandleValue;
    ULONG 	GrantedAccess;
    USHORT 	CreatorBackTraceIndex;
    USHORT 	ObjectTypeIndex;
    ULONG 	HandleAttributes;
    ULONG 	Reserved;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX, * PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SYSTEM_HANDLE_INFORMATION_EX
{
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
} SYSTEM_HANDLE_INFORMATION_EX, * PSYSTEM_HANDLE_INFORMATION_EX;

typedef NTSTATUS(NTAPI* pfn_NtQuerySystemInformation_t)(
    IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
    OUT PVOID SystemInformation,
    IN ULONG SystemInformationLength,
    OUT PULONG ReturnLength OPTIONAL
    );

// Undocumented system information class enum value
const SYSTEM_INFORMATION_CLASS SystemExtendedHandleInformation = SYSTEM_INFORMATION_CLASS(0x40);

typedef NTSTATUS(NTAPI* pfn_NtGetNextProcess_t)(
    _In_opt_ HANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Flags,
    _Out_ PHANDLE NewProcessHandle
    );

typedef NTSTATUS(NTAPI* pfn_NtGetNextThread_t)(
    _In_ HANDLE ProcessHandle,
    _In_opt_ HANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Flags,
    _Out_ PHANDLE NewThreadHandle
    );

typedef NTSTATUS(NTAPI* pfn_NtQueryInformationProcess_t)(
    _In_ HANDLE ProcessHandle,
    _In_ PROCESSINFOCLASS InfoClass,
    _Out_ PVOID ProcessInfo,
    _In_ ULONG ProcessInfoLen,
    _Out_ PULONG ReturnLength
    );

// The PROCESS_BASIC_INFORMATION definition in winternl.h is not useful for our purposes.
// This definition is from https://docs.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess
// Both definitions are the same size, whether compiling for x86 or x64.
typedef struct _PROCESS_BASIC_INFORMATION_FROM_DOCS {
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    ULONG_PTR AffinityMask;
    KPRIORITY BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION_FROM_DOCS;

#pragma warning (push)
#pragma warning (disable: 4201)
// Warning C4201: nonstandard extension used: nameless struct/union
typedef struct _PROCESS_EXTENDED_BASIC_INFORMATION {
    SIZE_T Size;    // Ignored as input, written with structure size on output
    PROCESS_BASIC_INFORMATION_FROM_DOCS BasicInfo;
    union {
        ULONG Flags;
        struct {
            ULONG IsProtectedProcess : 1;
            ULONG IsWow64Process : 1;
            ULONG IsProcessDeleting : 1;
            ULONG IsCrossSessionCreate : 1;
            ULONG IsFrozen : 1;
            ULONG IsBackground : 1;
            ULONG IsStronglyNamed : 1;
            ULONG IsSecureProcess : 1;
            ULONG IsSubsystemProcess : 1;
            ULONG SpareBits : 23;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
} PROCESS_EXTENDED_BASIC_INFORMATION, * PPROCESS_EXTENDED_BASIC_INFORMATION;
#pragma warning (pop)

