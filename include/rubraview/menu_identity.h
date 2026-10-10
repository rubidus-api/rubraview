#ifndef RUBRAVIEW_MENU_IDENTITY_H
#define RUBRAVIEW_MENU_IDENTITY_H

/*
 * D-86: the names Rubraview's Windows 11 menu goes by. The viewer's
 * registration and the package's manifest (scripts/make-menu-package.py
 * reads this file) must say the same; they are written once, here.
 */

/* The COM class of the "Rubraview" item; its values are kept under
   Software\Rubraview\Menu\{this}. */
#define RUBRAVIEW_MENU_CLSID "DEA03208-26B9-42F8-A239-14AAFB1527DC"

/* The identity package. The publisher is the form Windows asks of an
   unsigned package; the publisher id is derived from it (the first 8
   bytes of SHA-256 over its UTF-16LE text, in base 32) and is checked by
   the script when the package is made. */
#define RUBRAVIEW_MENU_PACKAGE_NAME "RubidusApi.Rubraview.Menu"
#define RUBRAVIEW_MENU_PACKAGE_PUBLISHER "CN=rubidus-api, OID.2.25.311729368913984317654407730594956997722=1"
#define RUBRAVIEW_MENU_PACKAGE_PUBLISHER_ID "6rh9jd4bpywzw"

/* The two files that sit beside rubraview.exe. */
#define RUBRAVIEW_MENU_DLL_FILE "rubraview_menu.dll"
#define RUBRAVIEW_MENU_PACKAGE_FILE "rubraview_menu.msix"

#endif /* RUBRAVIEW_MENU_IDENTITY_H */
