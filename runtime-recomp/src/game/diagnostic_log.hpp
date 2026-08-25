#pragma once

// The diagnostic log's one operation that has to be reachable from outside
// `game_main.cpp`.
//
// `RedirectDiagnosticsToFile` points stderr at `DKRR.LOG` and leaves it
// unbuffered, which is enough for a crash: the exception filter closes the
// stream. It is **not** enough for a run stopped from the outside. Windows 95
// updates a file's directory entry -- size and first cluster -- only at close,
// so a program killed with the emulator leaves an entry reading zero whatever it
// wrote. The bytes are on the disk, referenced by nothing: measured on 25 August
// 2026, `DKRR.LOG 0` in the directory against 526 lost clusters, 4,208 KB, on
// the transfer volume.
//
// `dkr_diag_commit` calls `_commit`, hence `FlushFileBuffers`, which forces the
// cache **and** the directory entry. Its own definition carried that reasoning
// from the start and nothing ever called it, so the property it describes was
// never true of any run. The declaration lives in a header rather than being
// repeated at each call site, so that the definition and its callers cannot
// drift apart in silence -- which is the failure mode this whole file is about.
extern "C" void dkr_diag_commit(void);
