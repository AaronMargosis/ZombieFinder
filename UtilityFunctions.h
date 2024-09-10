// Miscellaneous utility functions

#pragma once

#include <Windows.h>
#include <locale>


/// <summary>
/// Gets the executable image path associated with a Process ID, if that process is running
/// </summary>
/// <param name="pid">Input: process ID</param>
/// <param name="sProcessImagePath">Output: full image path of executable, if running; empty string otherwise</param>
/// <returns>true if successful</returns>
bool GetImagePathFromPID(ULONG_PTR pid, std::wstring& sProcessImagePath);

/// <summary>
/// Gets the executable image path of the parent process, if possible.
/// "Possible" means that the input parent process ID is a still-running process and that its start time
/// is earlier than the child process start time.
/// </summary>
/// <param name="ppid">Input: parent process ID</param>
/// <param name="ftChildStartTime">Input: child process start time</param>
/// <param name="sProcessImagePath">Output: full image path of executable associated with ppid</param>
/// <returns>true if successful</returns>
bool GetParentProcessImagePathIfStillRunning(ULONG_PTR ppid, const FILETIME& ftChildStartTime, std::wstring& sProcessImagePath);

/// <summary>
/// Converts the input number of total seconds into an English-language string incorporating "days", "hrs", "min" as appropriate
/// Examples:
/// Ago(90) --> "1 min 30 secs"
/// Ago(100000) --> "1 day 3 hrs 46 min 40 secs"
/// </summary>
std::wstring Ago(ULONGLONG nSecondsAgo);

