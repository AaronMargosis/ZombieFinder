#pragma once

#include <Windows.h>
#include <string>
#include <list>

/// <summary>
/// Structure that contains a service's key name and display name
/// </summary>
struct ServiceNames_t
{
	std::wstring sServiceName, sDisplayName;
};
/// <summary>
/// List of structures containing service information.
/// </summary>
typedef std::list<ServiceNames_t> ServiceList_t;

/// <summary>
/// If the input process ID is a service process, return the service and display names of those services.
/// </summary>
/// <param name="dwPID">Input: process ID</param>
/// <param name="pServiceList">Output: if the process is a service process, returns a pointer information about the services it hosts; NULL otherwise.</param>
/// <returns>true if the process is a service process; false otherwise</returns>
bool LookupServicesByPID(ULONG_PTR pid, const ServiceList_t** ppServiceList);

/// <summary>
/// For diagnostic purposes, dump the PID to services information to an ostream in human-readable form.
/// </summary>
/// <param name="szOutFile">Input: full path to output file</param>
/// <param name="bAppend">Input: true to append to the file; false to overwrite it</param>
/// <param name="sErrorInfo">Output: Information about any errors on failure</param>
/// <returns>true if successful</returns>
bool DumpPIDtoServiceLookupInfo(const wchar_t* szOutFile, bool bAppend, std::wstring& sErrorInfo);

