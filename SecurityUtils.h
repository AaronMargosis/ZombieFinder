#pragma once

#include <string>

/// <summary>
/// Enable a privilege if possible (present in current thread token).
/// CALLER SHOULD HAVE CALLED ImpersonateSelf PRIOR TO THIS
/// </summary>
/// <param name="szPrivilege">In: Name of privilege; e.g., SE_DEBUG_NAME</param>
/// <param name="sErrorInfo">Out: error information, on failure</param>
/// <returns>true if successful, false otherwise</returns>
bool EnablePrivilege(const wchar_t* szPrivilege, std::wstring& sErrorInfo);
