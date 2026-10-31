# Rubraview Privacy Policy

Effective date: October 31, 2026

This policy covers Rubraview, an image, comic book and video viewer for Windows, whether you get it from the Microsoft Store, the installer or the standalone download.

## Summary

Rubraview does not collect, send or share any personal information. It works only with files on your device, or on drives you open from it, and it does not connect to the internet.

## Information we collect

None. Rubraview has no accounts, advertising, analytics, telemetry or crash reporting of its own. It does not send anything to the developer or to anyone else.

## Information stored on your device

To work as you expect, Rubraview keeps a few plain-text files on your device. They never leave it.

- **Settings** (`settings.ini`, `keymap.ini`, `layout.ini`): your options, keyboard shortcuts and window layout.
- **History** (`history.ini`, `recent.ini`): the files you opened recently and where you stopped reading or watching. You can turn off the recent-files list, keep chosen folders out of it, or clear the reading history in Settings.
- **Favorites** (`favorites.ini`): the files and folders you mark as favorites.
- **Media facts** (`media-facts.txt`): the size and length of pictures, videos and music in folders and lists you have looked at, with their paths, so they need not be read again. It is not written while the recent-files list is turned off, and nothing from folders kept out of it goes in.
- **Lists** (`.rvlist`, `.m3u8`): saved only when you save a list yourself, at the place you choose.

These files are kept in your user profile's application data folder or, if you run Rubraview as a portable program, in its own folder. Uninstalling Rubraview or deleting these files removes this information.

Rubraview may write short-lived working files to Windows' temporary folder, for example when Explorer passes it a long selection of files. Thumbnails are kept in memory only.

## Your files and their metadata

Rubraview reads the pictures, videos, subtitles and archives you open, including metadata in them such as EXIF data, camera details and GPS location. It shows this only on your screen and never sends it anywhere. When you export or convert a picture, you can choose to remove location and other personal metadata from the copy.

Passwords you enter for encrypted archives are kept in memory only while the archive is open. They are never written to disk.

Rubraview changes, moves, copies or deletes your files only when you ask it to.

## Windows integration

When you ask for it, Rubraview registers itself with Windows so that it can open file types and appear in Explorer's menus. This information is stored in the Windows registry on your device. You can remove it from Rubraview's settings, and uninstalling Rubraview removes it.

If you put FFmpeg libraries on your device for extra video formats, Rubraview loads them locally. It does not download them.

The clipboard is used only when you copy something yourself.

## Microsoft Store and Windows

If you install Rubraview from the Microsoft Store, Microsoft may collect information about the download, installation and use of apps, as described in the [Microsoft Privacy Statement](https://privacy.microsoft.com/privacystatement). The developer does not control this and receives only the aggregate reports Microsoft provides to app publishers. Error reports collected by Windows are handled under the same statement.

## Children

Rubraview collects no information from anyone, including children under 13.

## Changes to this policy

If this policy changes, the new version will be published at this address with a new effective date.

## Contact

If you have questions about this policy, please open an issue at <https://github.com/rubidus-api/rubraview/issues>.
