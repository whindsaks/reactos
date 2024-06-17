#ifndef _RAPPS_H
#define _RAPPS_H

#if DBG && !defined(_DEBUG)
#define _DEBUG // CORE-17505
#endif

#include "defines.h"

#include "dialogs.h"
#include "appinfo.h"
#include "appdb.h"
#include "misc.h"
#include "configparser.h"

#define WM_UPDATEMENUITEM (WM_APP + 1)

extern HMENU g_ActiveContextMenu;

#endif /* _RAPPS_H */
