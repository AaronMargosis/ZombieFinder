# ZombieFinder
Identify zombie processes/threads and their reasons for being undead.

A _zombie process_ is one that has exited but that is still represented in kernel memory. This happens when another process retains a handle to the process or to one of its threads and fails to release it, or when something in the kernel obtains a similar reference and fails to dereference it after no longer needing it. System performance can suffer when zombies accumulate in large numbers. This utility identifies zombie processes and the processes responsible for keeping them "undead," as well as zombie processes that are being kept alive solely because of kernel references.

A process that has recently exited should not be considered a zombie. A process that has been waiting on another process' exit should be granted some time to retrieve its exit code and otherwise clean up its references. By default, this utility considers a process to be a zombie only if it had exited at least three seconds ago; this threshold can be changed with a command-line parameter.

The `-threads` option lists all process objects on the system, indicating whether each has exited, how many active and exited thread objects are associated with it, and its handle count.

`ZombieFinder.exe` works on x64 SKUs of Windows 7 / Windows Server 2008 R2 and newer.<br>
`ZombieFinder32.exe` works on x86 SKUs of Windows 7 / Windows Server 2008 R2 and newer.

Command-line syntax:
```
  ZombieFinder.exe [-details] [-csv] [-secs exitAgeInSecs] [-out filename] [-diag directory]
  ZombieFinder.exe -threads [-out filename]

    -details
      Outputs details about all zombies and owners; default is to output a summary.

    -csv
      Outputs results as tab-delimited fields; default is to output human-readable format with spacing.

    -secs exitAgeInSecs
      Consider a process to be a zombie only if it exited at least exitAgeInSecs seconds ago.
      Default is 3 seconds.

    -threads
      List all processes and counts of active and zombied threads in each (tab-delimited).

    -out filename
      Write output to filename. If not specified, writes to stdout.

    -diag directory
      Write diagnostic output - all collected handle and zombie information - to uniquely named files
      in the named directory.
```
