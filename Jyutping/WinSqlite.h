#pragma once

#include <sdkddkver.h>

// Windows SDK 10.0.28000 uses sqlite3_filename outside the version guard that defines it.
#if defined(NTDDI_WIN11_ZN) && NTDDI_VERSION < NTDDI_WIN11_ZN
using sqlite3_filename = const char*;
#endif

#include <winsqlite/winsqlite3.h>
