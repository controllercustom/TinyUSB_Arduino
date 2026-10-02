/* FatFs configuration for R4TinyUSB msc_file_explorer (FatFs R0.15).
 * Matches upstream example (R/W, no mkfs, 512 B sectors, 4 volumes) with
 * LFN off and tiny buffers, to fit the smallest supported SRAM budget.
 * ff.c / ff.h / diskio.h are ChaN FatFs R0.15 (stm32duino mirror, elm-chan
 * unreachable from here); diskio.c below glues disk_* to tuh_msc_*. */

#define FFCONF_DEF	80286	/* Revision ID: must match FF_DEFINED in ff.h */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	0
#define FF_USE_FIND		0
#define FF_USE_MKFS		0
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437
#define FF_USE_LFN		0
#define FF_MAX_LFN		32
#define FF_LFN_UNICODE	0
#define FF_LFN_BUF		32
#define FF_SFN_BUF		12
#define FF_FS_RPATH		2
#define FF_PATH_DEPTH	10

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		4
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS	"USB","USB2","USB3","USB4"
#define FF_MULTI_PARTITION	0
#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		0
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		1
#define FF_FS_EXFAT		0
#define FF_FS_NORTC		1
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
#define FF_FS_CRTIME	0
#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK		0
#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000
