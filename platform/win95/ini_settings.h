/* E06-S05 - DKRR.INI, the configuration file of the Windows 95 target.
 *
 * Every setting of this port is a `DKR_*` environment variable, read where it is
 * used and often by a static initialiser, before `main`. Rather than give each
 * one a second reader, the `[Settings]` section of DKRR.INI -- beside the
 * executable -- is copied into the C runtime's environment before any of them
 * runs: `RDRAM_SNAPSHOT=copy` in the file is `DKR_RDRAM_SNAPSHOT=copy` for the
 * code. A variable already set in the environment wins, so a batch file can
 * still override the file for one run.
 *
 * The copy happens in a constructor of priority 101, ahead of the C++ static
 * initialisers. `[Paths]` belongs to the ROM search (`game_main.cpp`).
 */
#ifndef DKR_WIN95_INI_SETTINGS_H
#define DKR_WIN95_INI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The full path of DKRR.INI, beside the executable. */
const char *dkr_ini_path(void);

/* How many settings the file supplied, and how many the environment overrode.
   For the log: the constructor runs before it exists. */
int dkr_ini_settings_applied(void);
int dkr_ini_settings_overridden(void);

/* Writes the commented template if DKRR.INI does not exist yet. Returns 1 if it
   wrote one. Called by the game, not by the constructor: the tools linked with
   this library have no business creating it. */
int dkr_ini_write_template_if_absent(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_INI_SETTINGS_H */
