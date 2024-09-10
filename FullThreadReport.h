#pragma once

#include <iostream>

/// <summary>
/// Lists all process objects on the system, indicating whether each has exited, how many active and exited thread objects 
/// are associated with it, and its handle count.
/// </summary>
/// <param name="pStream">Output: stream to write report to</param>
/// <returns>true if successful, false otherwise.</returns>
bool FullThreadReport(std::wostream* pStream);
