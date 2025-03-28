
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <wingdi.h>
#include <winuser.h>
#include <winbase.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(win);

typedef enum DISPLAYCONFIG_TOPOLOGY_ID
{
    DISPLAYCONFIG_TOPOLOGY_INTERNAL       = 0x00000001,
    DISPLAYCONFIG_TOPOLOGY_CLONE          = 0x00000002,
    DISPLAYCONFIG_TOPOLOGY_EXTEND         = 0x00000004,
    DISPLAYCONFIG_TOPOLOGY_EXTERNAL       = 0x00000008,
    DISPLAYCONFIG_TOPOLOGY_FORCE_UINT32   = 0xFFFFFFFF
} DISPLAYCONFIG_TOPOLOGY_ID;

typedef enum
{
    DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME                 = 1,
    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME                 = 2,
    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE       = 3,
    DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME                = 4,
    DISPLAYCONFIG_DEVICE_INFO_SET_TARGET_PERSISTENCE          = 5,
    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE            = 6,
    DISPLAYCONFIG_DEVICE_INFO_GET_SUPPORT_VIRTUAL_RESOLUTION  = 7,
    DISPLAYCONFIG_DEVICE_INFO_SET_SUPPORT_VIRTUAL_RESOLUTION  = 8,
    DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO         = 9,
    DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE        = 10,
    DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL             = 11,
    DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_SPECIALIZATION      = 12,
    DISPLAYCONFIG_DEVICE_INFO_SET_MONITOR_SPECIALIZATION      = 13,
    DISPLAYCONFIG_DEVICE_INFO_SET_RESERVED1                   = 14,
    DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2       = 15,
    DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE                   = 16,
    DISPLAYCONFIG_DEVICE_INFO_SET_WCG_STATE                   = 17,
    DISPLAYCONFIG_DEVICE_INFO_FORCE_UINT32                    = 0xFFFFFFFF
} DISPLAYCONFIG_DEVICE_INFO_TYPE;

typedef struct DISPLAYCONFIG_DEVICE_INFO_HEADER
{
    DISPLAYCONFIG_DEVICE_INFO_TYPE  type;
    UINT32                          size;
    LUID                            adapterId;
    UINT32                          id;
} DISPLAYCONFIG_DEVICE_INFO_HEADER;

typedef struct DISPLAYCONFIG_DESKTOP_IMAGE_INFO {
    POINTL PathSourceSize;
    RECTL  DesktopImageRegion;
    RECTL  DesktopImageClip;
} DISPLAYCONFIG_DESKTOP_IMAGE_INFO;

typedef enum {
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER = -1,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15 = 0,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO = 1,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO = 2,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO = 3,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI = 4,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI = 5,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS = 6,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_D_JPN = 8,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI = 9,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL = 10,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED = 11,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL = 12,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED = 13,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE = 14,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST = 15,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED = 16,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL = 17,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL = 0x80000000,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY;

typedef enum {
    DISPLAYCONFIG_ROTATION_IDENTITY = 1,
    DISPLAYCONFIG_ROTATION_ROTATE90 = 2,
    DISPLAYCONFIG_ROTATION_ROTATE180 = 3,
    DISPLAYCONFIG_ROTATION_ROTATE270 = 4,
    DISPLAYCONFIG_ROTATION_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_ROTATION;

typedef enum {
    DISPLAYCONFIG_SCALING_IDENTITY = 1,
    DISPLAYCONFIG_SCALING_CENTERED = 2,
    DISPLAYCONFIG_SCALING_STRETCHED = 3,
    DISPLAYCONFIG_SCALING_ASPECTRATIOCENTEREDMAX = 4,
    DISPLAYCONFIG_SCALING_CUSTOM = 5,
    DISPLAYCONFIG_SCALING_PREFERRED = 128,
    DISPLAYCONFIG_SCALING_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_SCALING;

typedef struct DISPLAYCONFIG_RATIONAL {
    UINT32 Numerator;
    UINT32 Denominator;
} DISPLAYCONFIG_RATIONAL;

typedef enum {
    DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED = 0,
    DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE = 1,
    DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED = 2,
    DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_UPPERFIELDFIRST,
    DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_LOWERFIELDFIRST = 3,
    DISPLAYCONFIG_SCANLINE_ORDERING_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_SCANLINE_ORDERING;

typedef struct DISPLAYCONFIG_PATH_TARGET_INFO {
    LUID                                  adapterId;
    UINT32                                id;
    union {
        UINT32 modeInfoIdx;
        struct {
            UINT32 desktopModeInfoIdx : 16;
            UINT32 targetModeInfoIdx : 16;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY outputTechnology;
    DISPLAYCONFIG_ROTATION                rotation;
    DISPLAYCONFIG_SCALING                 scaling;
    DISPLAYCONFIG_RATIONAL                refreshRate;
    DISPLAYCONFIG_SCANLINE_ORDERING       scanLineOrdering;
    BOOL                                  targetAvailable;
    UINT32                                statusFlags;
} DISPLAYCONFIG_PATH_TARGET_INFO;

typedef struct DISPLAYCONFIG_PATH_SOURCE_INFO {
    LUID   adapterId;
    UINT32 id;
    union {
        UINT32 modeInfoIdx;
        struct {
          UINT32 cloneGroupId : 16;
          UINT32 sourceModeInfoIdx : 16;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
  UINT32 statusFlags;
} DISPLAYCONFIG_PATH_SOURCE_INFO;

typedef struct DISPLAYCONFIG_PATH_INFO {
    DISPLAYCONFIG_PATH_SOURCE_INFO sourceInfo;
    DISPLAYCONFIG_PATH_TARGET_INFO targetInfo;
    UINT32                         flags;
} DISPLAYCONFIG_PATH_INFO;

typedef enum {
    DISPLAYCONFIG_PIXELFORMAT_8BPP = 1,
    DISPLAYCONFIG_PIXELFORMAT_16BPP = 2,
    DISPLAYCONFIG_PIXELFORMAT_24BPP = 3,
    DISPLAYCONFIG_PIXELFORMAT_32BPP = 4,
    DISPLAYCONFIG_PIXELFORMAT_NONGDI = 5,
    DISPLAYCONFIG_PIXELFORMAT_FORCE_UINT32 = 0xffffffff
} DISPLAYCONFIG_PIXELFORMAT;

typedef struct DISPLAYCONFIG_SOURCE_MODE
{
    UINT32                      width;
    UINT32                      height;
    DISPLAYCONFIG_PIXELFORMAT   pixelFormat;
    POINTL                      position;
} DISPLAYCONFIG_SOURCE_MODE;
typedef struct DISPLAYCONFIG_2DREGION {
    UINT32 cx;
    UINT32 cy;
} DISPLAYCONFIG_2DREGION;

typedef struct DISPLAYCONFIG_VIDEO_SIGNAL_INFO {
    UINT64                          pixelRate;
    DISPLAYCONFIG_RATIONAL          hSyncFreq;
    DISPLAYCONFIG_RATIONAL          vSyncFreq;
    DISPLAYCONFIG_2DREGION          activeSize;
    DISPLAYCONFIG_2DREGION          totalSize;
    union {
        struct {
            UINT32 videoStandard : 16;
            UINT32 vSyncFreqDivider : 6;
            UINT32 reserved : 10;
        } AdditionalSignalInfo;
        UINT32 videoStandard;
    } DUMMYUNIONNAME;
  DISPLAYCONFIG_SCANLINE_ORDERING scanLineOrdering;
} DISPLAYCONFIG_VIDEO_SIGNAL_INFO;
typedef struct DISPLAYCONFIG_TARGET_MODE
{
    DISPLAYCONFIG_VIDEO_SIGNAL_INFO   targetVideoSignalInfo;
} DISPLAYCONFIG_TARGET_MODE;
typedef enum {
    DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE = 1,
    DISPLAYCONFIG_MODE_INFO_TYPE_TARGET = 2,
    DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE = 3,
    DISPLAYCONFIG_MODE_INFO_TYPE_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_MODE_INFO_TYPE;
typedef struct DISPLAYCONFIG_MODE_INFO {
    DISPLAYCONFIG_MODE_INFO_TYPE infoType;
    UINT32                       id;
    LUID                         adapterId;
    union {
        DISPLAYCONFIG_TARGET_MODE        targetMode;
        DISPLAYCONFIG_SOURCE_MODE        sourceMode;
        DISPLAYCONFIG_DESKTOP_IMAGE_INFO desktopImageInfo;
    } DUMMYUNIONNAME;
} DISPLAYCONFIG_MODE_INFO;


LONG
WINAPI 
GetDisplayConfigBufferSizes(
    UINT32 flags,
    UINT32 *numPathArrayElements,
    UINT32 *numModeInfoArrayElements)
{
  *numPathArrayElements = 0;
  *numModeInfoArrayElements = 0;
  return 0;
}

LONG
WINAPI
QueryDisplayConfig(
    UINT32                    flags,
    UINT32                    *numPathArrayElements,
    DISPLAYCONFIG_PATH_INFO   *pathArray,
    UINT32                    *numModeInfoArrayElements,
    DISPLAYCONFIG_MODE_INFO   *modeInfoArray,
    DISPLAYCONFIG_TOPOLOGY_ID *currentTopologyId)
{
  return ERROR_ACCESS_DENIED;
}

typedef enum ORIENTATION_PREFERENCE {
    ORIENTATION_PREFERENCE_NONE              = 0x0,
    ORIENTATION_PREFERENCE_LANDSCAPE         = 0x1,
    ORIENTATION_PREFERENCE_PORTRAIT          = 0x2,
    ORIENTATION_PREFERENCE_LANDSCAPE_FLIPPED = 0x4,
    ORIENTATION_PREFERENCE_PORTRAIT_FLIPPED  = 0x8
} ORIENTATION_PREFERENCE;

/***********************************************************************
 *              DisplayConfigGetDeviceInfo (USER32.@)
 */
LONG WINAPI DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER *packet)
{
    FIXME( "DisplayConfigGetDeviceInfo: stub!\n" );
    return 1;
}

/***********************************************************************
 *              DisplayConfigSetDeviceInfo (USER32.@)
 */
LONG WINAPI DisplayConfigSetDeviceInfo( DISPLAYCONFIG_DEVICE_INFO_HEADER *packet )
{
    FIXME( "DisplayConfigSetDeviceInfo: stub!\n" );
    return 1;
}

/**********************************************************************
 *              GetDisplayAutoRotationPreferences (USER32.@)
 */
BOOL WINAPI GetDisplayAutoRotationPreferences( ORIENTATION_PREFERENCE *orientation )
{
    FIXME("(%p): stub\n", orientation);
    *orientation = ORIENTATION_PREFERENCE_NONE;
    return TRUE;
}

/***********************************************************************
 *              SetDisplayConfig (USER32.@)
 */
LONG WINAPI SetDisplayConfig(UINT32 path_info_count, DISPLAYCONFIG_PATH_INFO *path_info, UINT32 mode_info_count,
        DISPLAYCONFIG_MODE_INFO *mode_info, UINT32 flags)
{
    FIXME("path_info_count %u, path_info %p, mode_info_count %u, mode_info %p, flags %#x stub.\n",
            path_info_count, path_info, mode_info_count, mode_info, flags);

    return ERROR_SUCCESS;
}

/**********************************************************************
 *              SetDisplayAutoRotationPreferences (USER32.@)
 */
BOOL WINAPI SetDisplayAutoRotationPreferences( ORIENTATION_PREFERENCE orientation )
{
    FIXME("(%d): stub\n", orientation);
    return TRUE;
}
