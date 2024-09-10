#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "FileOutput.h"
#include "ServiceLookupByPID.h"

typedef std::map<ULONG_PTR, ServiceList_t> ServiceLookupByPID_t;

static ServiceLookupByPID_t ServiceLookupByPID;
static bool bInitialized = false;

/// <summary>
/// Initialize the lookup object.
/// If it fails, it fails silently.
/// </summary>
static void InitializeServiceLookup()
{
	if (bInitialized)
		return;

	bInitialized = true;

	BOOL ret;
	DWORD dwLastErr;
	SC_HANDLE hSCM = NULL;
	LPENUM_SERVICE_STATUS_PROCESSW pServiceInfoBuffer = nullptr;
	DWORD cbBytesNeeded = 0, dwServicesReturned = 0, dwResumeHandle = 0;
	hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
	if (NULL == hSCM)
	{
		//dwLastErr = GetLastError();
		//std::wcerr << L"OpenSCManagerW failed; error " << dwLastErr << std::endl;
		//retval = -1;
		goto cleanup;
	}
#pragma warning(push)
#pragma warning(disable:6031) // False positive: "Return value ignored: 'EnumServicesStatusExW'"
	/*ret =*/ EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &cbBytesNeeded, &dwServicesReturned, &dwResumeHandle, nullptr);
#pragma warning(pop)
	dwLastErr = GetLastError();
	if (ERROR_MORE_DATA != dwLastErr)
	{
		//std::wcerr << L"EnumServicesStatusExW (first call) failed; error " << dwLastErr << std::endl;
		//retval = -2;
		goto cleanup;
	}
	// Add 50% in case other services have become active in between calls.
	cbBytesNeeded = cbBytesNeeded + cbBytesNeeded / 2;
	pServiceInfoBuffer = LPENUM_SERVICE_STATUS_PROCESSW(new BYTE[cbBytesNeeded]);
	ret = EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, (LPBYTE)pServiceInfoBuffer, cbBytesNeeded, &cbBytesNeeded, &dwServicesReturned, &dwResumeHandle, nullptr);
	if (!ret)
	{
		//std::wcerr << L"EnumServicesStatusExW (second call) failed; error " << dwLastErr << std::endl;
		//retval = -3;
		goto cleanup;
	}

	for (DWORD ix = 0; ix < dwServicesReturned; ++ix)
	{
#pragma warning(push)
#pragma warning(disable:6385) // False positive: "Reading invalid data from 'pServiceInfoBuffer'"
		const ENUM_SERVICE_STATUS_PROCESSW& svc = pServiceInfoBuffer[ix];
#pragma warning(pop)
		ServiceNames_t names;
		names.sDisplayName = svc.lpDisplayName;
		names.sServiceName = svc.lpServiceName;
		ServiceLookupByPID_t::iterator iSvc = ServiceLookupByPID.find(svc.ServiceStatusProcess.dwProcessId);
		if (iSvc == ServiceLookupByPID.end())
		{
			ServiceList_t list;
			list.push_back(names);
			ServiceLookupByPID[svc.ServiceStatusProcess.dwProcessId] = list;
		}
		else
		{
			iSvc->second.push_back(names);
		}
	}

cleanup:
	delete[](LPBYTE)pServiceInfoBuffer;
	if (NULL != hSCM)
		CloseServiceHandle(hSCM);
	//return retval;
}

/// <summary>
/// If the input process ID is a service process, return the service and display names of those services.
/// </summary>
/// <param name="dwPID">Input: process ID</param>
/// <param name="pServiceList">Output: if the process is a service process, returns a pointer information about the services it hosts; NULL otherwise.</param>
/// <returns>true if the process is a service process; false otherwise</returns>
bool LookupServicesByPID(ULONG_PTR pid, const ServiceList_t** ppServiceList)
{
	// Make sure the lookup object has been initialized
	InitializeServiceLookup();

	ServiceLookupByPID_t::const_iterator iter = ServiceLookupByPID.find(pid);
	if (iter == ServiceLookupByPID.end())
	{
		*ppServiceList = nullptr;
		return false;
	}
	else
	{
		*ppServiceList = &iter->second;
		return true;
	}
}

/// <summary>
/// For diagnostic purposes, dump the PID to services information to an ostream in human-readable form.
/// </summary>
/// <param name="szOutFile">Input: full path to output file</param>
/// <param name="bAppend">Input: true to append to the file; false to overwrite it</param>
/// <param name="sErrorInfo">Output: Information about any errors on failure</param>
/// <returns>true if successful</returns>
bool DumpPIDtoServiceLookupInfo(const wchar_t* szOutFile, bool bAppend, std::wstring& sErrorInfo)
{
	// Output file stream, optionally appending
	std::wofstream fs;
	if (!CreateFileOutput(szOutFile, fs, bAppend))
	{
		std::wstringstream strErrorInfo;
		strErrorInfo << L"DumpPIDtoServiceLookupInfo to " << szOutFile << L" fails";
		sErrorInfo = strErrorInfo.str();
		return false;
	}

	// Make sure the lookup object has been initialized
	InitializeServiceLookup();

	// Determine longest service name, for formatting.
	size_t nSvcNameFieldWidth = 0;
	for (
		ServiceLookupByPID_t::const_iterator iterLookup = ServiceLookupByPID.begin();
		iterLookup != ServiceLookupByPID.end();
		iterLookup++
		)
	{
		for (ServiceList_t::const_iterator iterSvc = iterLookup->second.begin();
			iterSvc != iterLookup->second.end();
			iterSvc++
			)
		{
			if (iterSvc->sServiceName.length() > nSvcNameFieldWidth)
				nSvcNameFieldWidth = iterSvc->sServiceName.length();
		}
	}

	nSvcNameFieldWidth += 3;

	for (
		ServiceLookupByPID_t::const_iterator iterLookup = ServiceLookupByPID.begin();
		iterLookup != ServiceLookupByPID.end();
		iterLookup++
		)
	{
		fs << L"PID: " << iterLookup->first << std::endl;
		for (ServiceList_t::const_iterator iterSvc = iterLookup->second.begin();
			iterSvc != iterLookup->second.end();
			iterSvc++
			)
		{
			fs << L"             " << std::left << std::setw(nSvcNameFieldWidth) << iterSvc->sServiceName << L"  " << iterSvc->sDisplayName << std::endl;
		}
		fs << std::endl;
	}

	fs.close();

	return true;
}
