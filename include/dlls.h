/* $Id: dlls.h,v 1.2 1993/07/04 04:04:21 root Exp root $
 */
/*
 * Copyright  Robert J. Amstadt, 1993
 */

#ifndef DLLS_H
#define DLLS_H

#include "wintypes.h"

#define MAX_NAME_LENGTH		64


struct ne_data {
    struct ne_header_s *ne_header;
};

struct pe_data {
	struct pe_header_s *pe_header;
	struct pe_segment_table *pe_seg;
	struct PE_Import_Directory *pe_import;
	struct PE_Export_Directory *pe_export;
	struct PE_Resource_Directory *pe_resource;
	int resource_offset; /* offset to resource typedirectory in file */
};

struct w_files
{
    struct w_files  * next;
    char * name;   /* Name, as it appears in the windows binaries */
    char * filename; /* Actual name of the unix file that satisfies this */
    int type;        /* DLL or EXE */
    int fd;
    unsigned short hinstance;
    HANDLE hModule;
    int initialised;
    struct mz_header_s *mz_header;
    struct ne_data *ne;
    struct pe_data *pe;
};

extern struct  w_files *wine_files;

#define DLL	0
#define EXE	1

struct dll_table_entry_s
{
    /*
     * 16->32 bit interface data
     */
    char *export_name;
#ifdef WINESTAT
    int used;			/* Number of times this function referenced */
#endif
};

struct dll_table_s
{
    struct dll_table_entry_s *dll_table;
    int dll_table_length;
    int dll_number;
    BYTE *code_start;    /* 32-bit address of DLL code */
    BYTE *data_start;    /* 32-bit address of DLL data */
    BYTE *module_start;  /* 32-bit address of the module data */
    BYTE *module_end;
    HMODULE hModule;
};

struct dll_name_table_entry_s
{
    char *dll_name;
    struct dll_table_s *table;
    int dll_is_used;   /* use MS provided if set to zero */
};

extern struct dll_table_s KERNEL_table;
extern struct dll_table_s USER_table;
extern struct dll_table_s GDI_table;
extern struct dll_table_s WIN87EM_table;
extern struct dll_table_s MMSYSTEM_table;
extern struct dll_table_s SHELL_table;
extern struct dll_table_s SOUND_table;
extern struct dll_table_s KEYBOARD_table;
extern struct dll_table_s WINSOCK_table;
extern struct dll_table_s STRESS_table;
extern struct dll_table_s SYSTEM_table;
extern struct dll_table_s TOOLHELP_table;
extern struct dll_table_s MOUSE_table;
extern struct dll_table_s COMMDLG_table;
extern struct dll_table_s OLE2_table;
extern struct dll_table_s OLE2CONV_table;
extern struct dll_table_s OLE2DISP_table;
extern struct dll_table_s OLE2NLS_table;
extern struct dll_table_s OLE2PROX_table;
extern struct dll_table_s OLECLI_table;
extern struct dll_table_s OLESVR_table;
extern struct dll_table_s COMPOBJ_table;
extern struct dll_table_s STORAGE_table;
extern struct dll_table_s WINPROCS_table;
extern struct dll_table_s DDEML_table;

#define N_BUILTINS	25

#endif /* DLLS_H */


