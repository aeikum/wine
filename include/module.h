/*
 * Module definitions
 *
 * Copyright 1995 Alexandre Julliard
 */

#ifndef _WINE_MODULE_H
#define _WINE_MODULE_H

#include "wintypes.h"

#ifndef WINELIB
#pragma pack(1)
#endif

  /* In-memory module structure. See 'Windows Internals' p. 219 */
typedef struct
{
    WORD    magic;            /* 'NE' signature */
    WORD    count;            /* Usage count */
    WORD    entry_table;      /* Near ptr to entry table */
    WORD    next;             /* Selector to next module */
    WORD    dgroup_entry;     /* Near ptr to segment entry for DGROUP */
    WORD    fileinfo;         /* Near ptr to file info (LOADEDFILEINFO) */
    WORD    flags;            /* Module flags */
    WORD    dgroup;           /* Logical segment for DGROUP */
    WORD    heap_size;        /* Initial heap size */
    WORD    stack_size;       /* Initial stack size */
    WORD    ip;               /* Initial ip */
    WORD    cs;               /* Initial cs (logical segment) */
    WORD    sp;               /* Initial stack pointer */
    WORD    ss;               /* Initial ss (logical segment) */
    WORD    seg_count;        /* Number of segments in segment table */
    WORD    modref_count;     /* Number of module references */
    WORD    nrname_size;      /* Size of non-resident names table */
    WORD    seg_table;        /* Near ptr to segment table */
    WORD    res_table;        /* Near ptr to resource table */
    WORD    name_table;       /* Near ptr to resident names table */
    WORD    modref_table;     /* Near ptr to module reference table */
    WORD    import_table;     /* Near ptr to imported names table */
    DWORD   nrname_fpos;      /* File offset of non-resident names table */
    WORD    moveable_entries; /* Number of moveable entries in entry table */
    WORD    alignment;        /* Alignment shift count */
    WORD    truetype;         /* Set to 2 if TrueType font */
    BYTE    os_flags;         /* Operating system flags */
    BYTE    misc_flags;       /* Misc. flags */
    WORD    reserved;         /* Same value as import_table */
    HANDLE  nrname_handle;    /* Handle to non-resident name table in memory */
    WORD    min_swap_area;    /* Min. swap area size */
    WORD    expected_version; /* Expected Windows version */
} NE_MODULE;

  /* Loaded file info */
typedef struct
{
    BYTE  length;       /* Length of the structure, not counting this byte */
    BYTE  fixed_media;  /* File is on removable media */
    WORD  error;        /* Error code (?) */
    WORD  date;         /* File date in MS-DOS format */
    WORD  time;         /* File time in MS-DOS format */
    char  filename[1];  /* File name */
} LOADEDFILEINFO;

  /* In-memory segment table */
typedef struct
{
    WORD    filepos;   /* Position in file, in sectors */
    WORD    size;      /* Segment size on disk */
    WORD    flags;     /* Segment flags */
    WORD    minsize;   /* Min. size of segment in memory */
    WORD    selector;  /* Selector of segment in memory */
} SEGTABLEENTRY;


#define NE_SEG_TABLE(pModule) \
    ((SEGTABLEENTRY *)((char *)(pModule) + (pModule)->seg_table))

#define NE_MODULE_TABLE(pModule) \
    ((WORD *)((char *)(pModule) + (pModule)->modref_table))

#ifndef WINELIB
#pragma pack(4)
#endif

extern BOOL MODULE_Init(void);
extern int MODULE_OpenFile( HMODULE hModule );
extern LPSTR MODULE_GetModuleName( HMODULE hModule );
extern WORD MODULE_GetOrdinal( HMODULE hModule, char *name );
extern DWORD MODULE_GetEntryPoint( HMODULE hModule, WORD ordinal );
extern void MODULE_FixupPrologs( HMODULE hModule );

#endif  /* _WINE_MODULE_H */
