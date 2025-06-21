// ZombieFinder.cpp : Command-line interface around zombie detection and owner identification
//

#include <iostream>
#include <sstream>
#include <fstream>
#include <locale>
#include <codecvt>
#include <io.h>
#include <fcntl.h>
#include "HEX.h"
#include "UtilityFunctions.h"
#include "StringUtils.h"
#include "FileOutput.h"
#include "ZombieOwners.h"
#include "FullThreadReport.h"

//TODO: Identify if handles are duplicates of one another

/// <summary>
/// Write command-line syntax to stderr and then exit.
/// </summary>
/// <param name="szError">Caller-supplied error text</param>
/// <param name="argv0">The program's argv[0] value</param>
void Usage(const wchar_t* szError, const wchar_t* argv0)
{
    std::wstring sExe = GetFileNameFromFilePath(argv0);
    if (szError)
        std::wcerr << szError << std::endl;
    std::wcerr
        << std::endl
        << L"Usage:" << std::endl
        << std::endl
        << L"  " << sExe << L" [-details] [-csv] [-secs exitAgeInSecs] [-out filename] [-diag directory]" << std::endl
        << L"  " << sExe << L" -threads [-out filename]" << std::endl
        << std::endl
        << L"    -details" << std::endl
        << L"      Outputs details about all zombies and owners; default is to output a summary." << std::endl
        << std::endl
        << L"    -csv" << std::endl
        << L"      Outputs results as tab-delimited fields; default is to output human-readable format with spacing." << std::endl
        << std::endl
        << L"    -secs exitAgeInSecs" << std::endl
        << L"      Consider a process to be a zombie only if it exited at least exitAgeInSecs seconds ago." << std::endl
        << L"      Default is 3 seconds." << std::endl
        << std::endl
        << L"    -threads" << std::endl
        << L"      List all processes and counts of active and zombied threads in each (tab-delimited)." << std::endl
        << std::endl
        << L"    -out filename" << std::endl
        << L"      Write output to filename. If not specified, writes to stdout." << std::endl
        << std::endl
        << L"    -diag directory" << std::endl
        << L"      Write diagnostic output - all collected handle and zombie information - to uniquely named files" << std::endl
        << L"      in the named directory." << std::endl
        << std::endl
        << std::endl;
    exit(-1);
}

// ----------------------------------------------------------------------------------------------------
// Forward declarations for output functions
void OutputSummary(const ZombieOwners& zombieOwners, ULONGLONG ulNow, std::wostream* pStream);
void OutputSummaryCsv(const ZombieOwners& zombieOwners, ULONGLONG ulNow, std::wostream* pStream);
void OutputDetails(const ZombieOwners& zombieOwners, ULONGLONG ulNow, std::wostream* pStream);
void OutputDetailsCsv(const ZombieOwners& zombieOwners, ULONGLONG ulNow, std::wostream* pStream);

const wchar_t* const szTabDelim = L"\t";

// ----------------------------------------------------------------------------------------------------
int wmain(int argc, wchar_t** argv)
{
    // Exit out if this is a 32-bit process on 64-bit Windows.
    BOOL bWow64Process = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &bWow64Process) && bWow64Process)
    {
        std::wcerr << L"Wrong version. You need to run the 64-bit version of this program." << std::endl;
        exit(-2);
    }

    // Set output mode to UTF8.
    if (_setmode(_fileno(stdout), _O_U8TEXT) == -1 || _setmode(_fileno(stderr), _O_U8TEXT) == -1)
    {
        std::wcerr << L"Unable to set stdout and/or stderr modes to UTF8." << std::endl;
    }

    bool bDetails = false, bCsv = false, bThreadsReport = false;
    ULONGLONG nExitAgeInSecs = 3;
    bool bOut_toFile = false;
    std::wstring sOutFile, sDiagDirectory;

    // Parse command line options
    int ixArg = 1;
    while (ixArg < argc)
    {
        if (0 == _wcsicmp(L"-details", argv[ixArg]))
        {
            bDetails = true;
        }
        else if (0 == _wcsicmp(L"-csv", argv[ixArg]))
        {
            bCsv = true;
        }
        else if (0 == _wcsicmp(L"-threads", argv[ixArg]))
        {
            bThreadsReport = true;
        }
        else if (0 == _wcsicmp(L"-secs", argv[ixArg]))
        {
            if (++ixArg >= argc)
                Usage(L"Missing arg for -secs", argv[0]);
            if (1 != swscanf_s(argv[ixArg], L"%llu", &nExitAgeInSecs))
                Usage(L"Invalid arg for -secs", argv[0]);
        }
        else if (0 == _wcsicmp(L"-out", argv[ixArg]))
        {
            bOut_toFile = true;
            if (++ixArg >= argc)
                Usage(L"Missing arg for -out", argv[0]);
            sOutFile = argv[ixArg];
        }
        else if (0 == _wcsicmp(L"-diag", argv[ixArg]))
        {
            if (++ixArg >= argc)
                Usage(L"Missing arg for -diag", argv[0]);
            sDiagDirectory = argv[ixArg];
        }
        else
        {
            // Show usage; no error message if command line param is -? or /?
            const wchar_t* szErrMsg =
                (0 == wcscmp(L"-?", argv[ixArg]) || 0 == wcscmp(L"/?", argv[ixArg])) ?
                NULL :
                L"Unrecognized command-line option";
            Usage(szErrMsg, argv[0]);
        }
        ++ixArg;
    }

    // Verify no invalid combination of switches
    if (bThreadsReport && (bDetails || bCsv || 3 != nExitAgeInSecs || sDiagDirectory.length() > 0))
    {
        Usage(L"Invalid combination of switches", argv[0]);
    }

    // If sDiagDirectory is specified, ensure that it exists and is a directory
    if (sDiagDirectory.size() > 0)
    {
        // Remove trailing path separator if it has one. (PowerShell autocomplete likes to append them, helpfully...)
        while (EndsWith(sDiagDirectory, L'\\') || EndsWith(sDiagDirectory, L'/'))
            sDiagDirectory = sDiagDirectory.substr(0, sDiagDirectory.length() - 1);

        DWORD dwAttributes = GetFileAttributesW(sDiagDirectory.c_str());
        if (
            INVALID_FILE_ATTRIBUTES == dwAttributes ||
            0 == (FILE_ATTRIBUTE_DIRECTORY & dwAttributes)
            )
        {
            Usage(L"-diag argument is not a directory", argv[0]);
        }
    }

    // Define a wostream output; create a UTF-8 wofstream if sOutFile defined; point it to *pStream otherwise.
    // pStream points to whatever ostream we're writing to.
    // Default to writing to stdout/wcout.
    // If -out specified, open an fstream for writing.
    std::wostream* pStream = &std::wcout;
    std::wofstream fs;
    if (bOut_toFile)
    {
        pStream = &fs;
        if (!CreateFileOutput(sOutFile.c_str(), fs, false))
        {
            // If opening the file for output fails, quit now.
            std::wcerr << L"Cannot open output file " << sOutFile << std::endl;
            Usage(NULL, argv[0]);
        }
    }

    int iExitCode = 0;

    if (bThreadsReport)
    {
        if (!FullThreadReport(pStream))
            iExitCode = -1;
    }
    else
    {    // Note: FILETIME, ULARGE_INTEGER, and ULONGLONG are all 8 bytes, and lay out the same way.
        ULONGLONG ulNow = 0;
        GetSystemTimeAsFileTime((LPFILETIME)&ulNow);

        // ------------------------------------------------------------------------------------------
        // Get all the info about zombie processes and their owners
        ZombieOwners zombieOwners;
        std::wstring sErrorInfo;
        if (zombieOwners.Update(nExitAgeInSecs, sDiagDirectory, sErrorInfo))
        {
            // Output:
            if (!bDetails)
            {
                if (!bCsv)
                    OutputSummary(zombieOwners, ulNow, pStream);
                else
                    OutputSummaryCsv(zombieOwners, ulNow, pStream);
            }
            else
            {
                if (!bCsv)
                    OutputDetails(zombieOwners, ulNow, pStream);
                else
                    OutputDetailsCsv(zombieOwners, ulNow, pStream);
            }
        }
        else
        {
            std::wcerr << L"Error: " << sErrorInfo << std::endl;
            iExitCode = -1;
        }
    }

    // ------------------------------------------------------------------------------------------
    // If output to a file, close the file.
    if (bOut_toFile)
    {
        fs.close();
    }

    return iExitCode;
}

// ------------------------------------------------------------------------------------------
/// <summary>
/// Output summary results in human-readable table format
/// </summary>
/// <param name="zombieOwners">Input: zombie process/owner information</param>
/// <param name="ulNow">Input: representation of current time (not used in this function)</param>
/// <param name="pStream">Input: pointer to output stream into which to write</param>
void OutputSummary(const ZombieOwners& zombieOwners, ULONGLONG /*ulNow*/, std::wostream* pStream)
{
    // Get the zombie owners, sorted by zombie handle counts in descending order
    const ZombieOwnersCollectionSorted_t& coll = zombieOwners.OwnersCollectionSorted();

    // Determine longest exe name, so the table can be properly formatted
    size_t nExeAndPidFieldWidth = 0;
    const size_t nCountFieldWidth = 6;

    for (ZombieOwnersCollectionSorted_t::const_iterator iter = coll.begin();
        iter != coll.end();
        iter++)
    {
        if ((*iter)->sExeName.length() > nExeAndPidFieldWidth)
            nExeAndPidFieldWidth = (*iter)->sExeName.length();
    }
    // Add to cover "(pid)" plus spaces
    nExeAndPidFieldWidth += 10;

    // Table headers
    *pStream << std::left << std::setw(nExeAndPidFieldWidth) << L"Exe name (PID)" << std::right << std::setw(nCountFieldWidth) << L"Count" << L"     Services" << std::endl;
    *pStream << std::left << std::setw(nExeAndPidFieldWidth) << L"--------------" << std::right << std::setw(nCountFieldWidth) << L"-----" << L"     --------" << std::endl;

    // Zombie owners, counts, and services (if any)
    for (ZombieOwnersCollectionSorted_t::const_iterator iter = coll.begin();
        iter != coll.end();
        iter++)
    {
        std::wstringstream str;
        str << (*iter)->sExeName << L" (" << (*iter)->PID << L")";
        *pStream
            << std::left << std::setw(nExeAndPidFieldWidth) << str.str() << std::right << std::setw(nCountFieldWidth) << (*iter)->zombieOwningInfo.size();
        if (nullptr != (*iter)->pServiceList)
        {
            *pStream << L"     ";
            for (
                ServiceList_t::const_iterator iterSvc = (*iter)->pServiceList->begin();
                iterSvc != (*iter)->pServiceList->end();
                iterSvc++
                )
            {
                *pStream << iterSvc->sServiceName << L" ";
            }
        }
        *pStream << std::endl;
    }

    // Zombie processes with no user-mode handles
    if (zombieOwners.UnexplainedZombies().size() > 0)
    {
        *pStream
            << std::left << std::setw(nExeAndPidFieldWidth) << L"(No process)" << std::right << std::setw(nCountFieldWidth) << zombieOwners.UnexplainedZombies().size() << std::endl;
    }

    // Any process enumeration errors
    if (zombieOwners.ProcessEnumErrors().size() > 0)
    {
        for (
            ProcessEnumErrorInfoList_t::const_iterator iter = zombieOwners.ProcessEnumErrors().begin();
            iter != zombieOwners.ProcessEnumErrors().end();
            iter++
            )
        {
            *pStream << L"ERROR: " << *iter << std::endl;
        }
    }
}

// ------------------------------------------------------------------------------------------
/// <summary>
/// Output summary results in tab-delimited fields
/// </summary>
/// <param name="zombieOwners">Input: zombie process/owner information</param>
/// <param name="ulNow">Input: representation of current time (not used in this function)</param>
/// <param name="pStream">Input: pointer to output stream into which to write</param>
void OutputSummaryCsv(const ZombieOwners& zombieOwners, ULONGLONG /*ulNow*/, std::wostream* pStream)
{
    // Get the zombie owners, sorted by zombie handle counts in descending order
    const ZombieOwnersCollectionSorted_t& coll = zombieOwners.OwnersCollectionSorted();

    // Table headers
    *pStream 
        << L"Exe name" << szTabDelim
        << L"PID" << szTabDelim
        << L"Count" << szTabDelim
        << L"Services" 
        << std::endl;

    // Zombie owners, counts, and services (if any)
    for (ZombieOwnersCollectionSorted_t::const_iterator iter = coll.begin();
        iter != coll.end();
        iter++)
    {
        *pStream
            << (*iter)->sExeName << szTabDelim
            << (*iter)->PID << szTabDelim
            << (*iter)->zombieOwningInfo.size() << szTabDelim;
        if (nullptr != (*iter)->pServiceList)
        {
            for (
                ServiceList_t::const_iterator iterSvc = (*iter)->pServiceList->begin();
                iterSvc != (*iter)->pServiceList->end();
                iterSvc++
                )
            {
                *pStream << iterSvc->sServiceName << L" ";
            }
        }
        *pStream << std::endl;
    }

    // Zombie processes with no user-mode handles
    if (zombieOwners.UnexplainedZombies().size() > 0)
    {
        *pStream
            << L"(No process)" << szTabDelim << szTabDelim << zombieOwners.UnexplainedZombies().size() << szTabDelim << std::endl;
    }

    // Any process enumeration errors
    if (zombieOwners.ProcessEnumErrors().size() > 0)
    {
        for (
            ProcessEnumErrorInfoList_t::const_iterator iter = zombieOwners.ProcessEnumErrors().begin();
            iter != zombieOwners.ProcessEnumErrors().end();
            iter++
            )
        {
            *pStream << L"ERROR: " << *iter << szTabDelim << szTabDelim << szTabDelim << std::endl;
        }
    }
}

// ------------------------------------------------------------------------------------------
/// <summary>
/// Output detailed results in (more or less) human-readable format
/// </summary>
/// <param name="zombieOwners">Input: zombie process/owner information</param>
/// <param name="ulNow">Input: representation of current time</param>
/// <param name="pStream">Input: pointer to output stream into which to write</param>
void OutputDetails(const ZombieOwners& zombieOwners, ULONGLONG ulNow, std::wostream* pStream)
{
    // High-level summary
    *pStream << L"Zombie processes: " << zombieOwners.ZombieProcessCount() << std::endl;
    *pStream << L"Zombie threads  : " << zombieOwners.ZombieProcessAndThreadCount() - zombieOwners.ZombieProcessCount() << std::endl;
    *pStream << std::endl;

    // Existing user-mode processes holding handles to zombies, and info about those zombies
    const ZombieOwnersCollectionSorted_t& coll = zombieOwners.OwnersCollectionSorted();
    for (
        ZombieOwnersCollectionSorted_t::const_iterator iterOwners = coll.begin();
        coll.end() != iterOwners;
        ++iterOwners
        )
    {
        const ZombieOwner_t& owner = **iterOwners;
        const ZombieOwningInfoList_t& owningInfo = owner.zombieOwningInfo;
        *pStream
            << owner.sExeName << L" (" << owner.PID << L") | Full path: " << owner.sProcessImagePath;
        if (nullptr != owner.pServiceList)
        {
            *pStream << L" | Service(s): ";
            for (
                ServiceList_t::const_iterator iterSvc = owner.pServiceList->begin();
                iterSvc != owner.pServiceList->end();
                iterSvc++
                )
            {
                *pStream << iterSvc->sServiceName << L" ";
            }
        }
        *pStream
            << std::endl
            << owningInfo.size() << L" zombie handle(s):" << std::endl;
        for (
            ZombieOwningInfoList_t::const_iterator iterOwningInfo = owningInfo.begin();
            owningInfo.end() != iterOwningInfo;
            ++iterOwningInfo
            )
        {
            const ZombieProcessThreadInfo& z = iterOwningInfo->zombieInfo;
            const ULONGLONG& ulExitTime = (*(const ULONGLONG*)&z.exitTime);
            ULONGLONG nSecondsAgo = (ulNow - ulExitTime) / 10000000;

            if (0 == z.TID)
            {
                *pStream << L"    Handle " << HEX(iterOwningInfo->handleValue) << L"  PID " << std::right << std::setw(6) << z.PID << L"  " << z.sImagePath << L" ; exited " << FileTimeToWString(z.exitTime, false) << L": " << Ago(nSecondsAgo) << L" ago" << std::endl;
            }
            else
            {
                *pStream << L"    Handle " << HEX(iterOwningInfo->handleValue) << L"  PID:TID " << z.PID << L":" << z.TID << L"  " << z.sImagePath << L" ; exited " << FileTimeToWString(z.exitTime, false) << L": " << Ago(nSecondsAgo) << L" ago" << std::endl;
            }
            *pStream << L"        Parent: " << z.ParentPID << L" " << (z.sParentImagePath.length() > 0 ? z.sParentImagePath : L"(exited)") << std::endl;
        }
        *pStream << std::endl;
    }

    // Information about zombie processes for which no user-mode handles could be found:
    if (zombieOwners.UnexplainedZombies().size() > 0)
    {
        *pStream
            << L"Zombie processes for which no handles were found:" << std::endl
            << zombieOwners.UnexplainedZombies().size() << L" process(es):" << std::endl;
        for (
            ZombieProcessThreadInfoList_t::const_iterator iterUnexplained = zombieOwners.UnexplainedZombies().begin();
            zombieOwners.UnexplainedZombies().end() != iterUnexplained;
            ++iterUnexplained
            )
        {
            const ZombieProcessThreadInfo& z = *iterUnexplained;
            const ULONGLONG& ulExitTime = (*(const ULONGLONG*)&z.exitTime);
            ULONGLONG nSecondsAgo = (ulNow - ulExitTime) / 10000000;

            *pStream
                << L"    PID " << z.PID << L"  " << z.sImagePath << std::endl
                << L"      Exited " << FileTimeToWString(z.exitTime, false) << L": " << Ago(nSecondsAgo) << L" ago" << std::endl
                << L"      Threads: " << z.nThreads << std::endl
                << L"      Parent: " << z.ParentPID << L" " << (z.sParentImagePath.length() > 0 ? z.sParentImagePath : L"(exited)") << std::endl;
        }
    }

    // Any process enumeration errors
    if (zombieOwners.ProcessEnumErrors().size() > 0)
    {
        for (
            ProcessEnumErrorInfoList_t::const_iterator iter = zombieOwners.ProcessEnumErrors().begin();
            iter != zombieOwners.ProcessEnumErrors().end();
            iter++
            )
        {
            *pStream << L"ERROR: " << *iter << std::endl;
        }
    }
}

// ------------------------------------------------------------------------------------------
/// <summary>
/// Output detailed results in tab-delimited fields
/// </summary>
/// <param name="zombieOwners">Input: zombie process/owner information</param>
/// <param name="ulNow">Input: representation of current time</param>
/// <param name="pStream">Input: pointer to output stream into which to write</param>
void OutputDetailsCsv(const ZombieOwners& zombieOwners, ULONGLONG ulNow, std::wostream* pStream)
{
    // Tab-delimited headers
    *pStream
        << L"Owning process name" << szTabDelim
        << L"Owning PID" << szTabDelim
        << L"Owning process image path" << szTabDelim
        << L"Services" << szTabDelim
        << L"Handle" << szTabDelim
        << L"Z PID" << szTabDelim
        << L"Z TID" << szTabDelim
        << L"Zombie image path" << szTabDelim
        << L"Threads" << szTabDelim
        << L"Started" << szTabDelim
        << L"Exited" << szTabDelim
        << L"Exited ago" << szTabDelim
        << L"PPID" << szTabDelim
        << L"Parent image path"
        << std::endl;

    // Existing user-mode processes holding handles to zombies, and info about those zombies
    const ZombieOwnersCollectionSorted_t& coll = zombieOwners.OwnersCollectionSorted();
    for (
        ZombieOwnersCollectionSorted_t::const_iterator iterOwners = coll.begin();
        coll.end() != iterOwners;
        ++iterOwners
        )
    {
        const ZombieOwner_t& owner = **iterOwners;
        const ZombieOwningInfoList_t& owningInfo = owner.zombieOwningInfo;
        for (
            ZombieOwningInfoList_t::const_iterator iterOwningInfo = owningInfo.begin();
            owningInfo.end() != iterOwningInfo;
            ++iterOwningInfo
            )
        {
            const ZombieProcessThreadInfo& z = iterOwningInfo->zombieInfo;
            const ULONGLONG& ulExitTime = (*(const ULONGLONG*)&z.exitTime);
            ULONGLONG nSecondsAgo = (ulNow - ulExitTime) / 10000000;

            // If it's a thread handle, populate the TID field with the Thread ID, and leave the Threads field empty.
            // If it's a process handle, populate the Threads field with the number of threads in the process, and leave the TID field empty.
            std::wstringstream strTID, strThreads;
            if (0 != z.TID)
            {
                strTID << z.TID;
            }
            else
            {
                strThreads << z.nThreads;
            }

            // First three tab-delimited fields
            *pStream
                << owner.sExeName << szTabDelim
                << owner.PID << szTabDelim
                << owner.sProcessImagePath << szTabDelim;
            // If the process hosts services, put their key names in the next field, separated by spaces
            if (nullptr != owner.pServiceList)
            {
                for (
                    ServiceList_t::const_iterator iterSvc = owner.pServiceList->begin();
                    iterSvc != owner.pServiceList->end();
                    iterSvc++
                    )
                {
                    *pStream << iterSvc->sServiceName << L" ";
                }
            }
            // Rest of the fields
            *pStream
                << szTabDelim // tab following the Services field
                << HEX(iterOwningInfo->handleValue, 8, false, true) << szTabDelim
                << z.PID << szTabDelim
                << strTID.str() << szTabDelim
                << z.sImagePath << szTabDelim
                << strThreads.str() << szTabDelim
                << FileTimeToWString(z.createTime, false) << szTabDelim
                << FileTimeToWString(z.exitTime, false) << szTabDelim
                << Ago(nSecondsAgo) << szTabDelim
                << z.ParentPID << szTabDelim
                << (z.sParentImagePath.length() > 0 ? z.sParentImagePath : L"(exited)")
                << std::endl;
        }
    }

    // Information about zombie processes for which no user-mode handles could be found:
    if (zombieOwners.UnexplainedZombies().size() > 0)
    {
        for (
            ZombieProcessThreadInfoList_t::const_iterator iterUnexplained = zombieOwners.UnexplainedZombies().begin();
            zombieOwners.UnexplainedZombies().end() != iterUnexplained;
            ++iterUnexplained
            )
        {
            const ZombieProcessThreadInfo& z = *iterUnexplained;
            const ULONGLONG& ulExitTime = (*(const ULONGLONG*)&z.exitTime);
            ULONGLONG nSecondsAgo = (ulNow - ulExitTime) / 10000000;

            // First five fields are empty - no user-mode processes found holding handles to these zombies. TID field empty as well.
            *pStream
                << szTabDelim
                << szTabDelim
                << szTabDelim
                << szTabDelim
                << szTabDelim
                << z.PID << szTabDelim
                << szTabDelim
                << z.sImagePath << szTabDelim
                << z.nThreads << szTabDelim
                << FileTimeToWString(z.createTime, false) << szTabDelim
                << FileTimeToWString(z.exitTime, false) << szTabDelim
                << Ago(nSecondsAgo) << szTabDelim
                << z.ParentPID << szTabDelim
                << (z.sParentImagePath.length() > 0 ? z.sParentImagePath : L"(exited)")
                << std::endl;
        }
    }

    // Any process enumeration errors
    if (zombieOwners.ProcessEnumErrors().size() > 0)
    {
        for (
            ProcessEnumErrorInfoList_t::const_iterator iter = zombieOwners.ProcessEnumErrors().begin();
            iter != zombieOwners.ProcessEnumErrors().end();
            iter++
            )
        {
            // First five fields are empty - no user-mode processes found holding handles to these zombies. TID field empty as well.
            *pStream
                << L"ERROR" << szTabDelim // Owning process name
                << L"ERROR" << szTabDelim // Owning PID
                << *iter << szTabDelim // Owning process image path
                << szTabDelim // Services
                << szTabDelim // Handle
                << szTabDelim // Z PID
                << szTabDelim // Z TID
                << szTabDelim // Zombie image path
                << szTabDelim // Threads
                << szTabDelim // Started
                << szTabDelim // Exited
                << szTabDelim // Exited ago
                << szTabDelim // PPID
                << std::endl; // Parent image path
        }
    }
}






