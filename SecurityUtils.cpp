
#include <Windows.h>
#include <string>
#include "SysErrorMessage.h"
#include "SecurityUtils.h"

/// <summary>
/// Enable a privilege if possible (present in current thread token).
/// CALLER SHOULD HAVE CALLED ImpersonateSelf PRIOR TO THIS
/// </summary>
/// <param name="szPrivilege">In: Name of privilege; e.g., SE_DEBUG_NAME</param>
/// <param name="sErrorInfo">Out: error information, on failure</param>
/// <returns>true if successful, false otherwise</returns>
bool EnablePrivilege(const wchar_t* szPrivilege, std::wstring& sErrorInfo)
{
	BOOL ret;
	DWORD dwLastErr;
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp = { 0 };

	// Caller must be impersonating - should have called ImpersonateSelf so that we're not
	// modifying privileges in the process token, just in the current thread.
	// This call will fail if not impersonating - threads don't get their own tokens by default.
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &hToken))
	{
		sErrorInfo = SysErrorMessageWithCode();
		return false;
	}

	ret = LookupPrivilegeValueW(NULL, szPrivilege, &tkp.Privileges[0].Luid);
	dwLastErr = GetLastError();
	if (ret)
	{
		tkp.PrivilegeCount = 1;  // one privilege to set    
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		ret = AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, NULL);
		dwLastErr = GetLastError();
	}

	CloseHandle(hToken);

	if (!ret || ERROR_SUCCESS != dwLastErr)
	{
		sErrorInfo = SysErrorMessageWithCode(dwLastErr);
		return false;
	}

	return true;
}
