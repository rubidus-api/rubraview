#!/usr/bin/env python3
"""Makes rubraview_menu.msix: the identity package of the Windows 11 menu (D-86).

Windows 11 shows in its context menu only what a package with identity
declares. This one holds a manifest and three logos and nothing else: it
is registered with the folder of rubraview.exe as its external location,
and names rubraview_menu.dll there as the server of one COM class, shown
on every extension the viewer knows (the DLL hides it on those the reader
did not choose). The layout follows Rubrapack's rp_msix_sparse.

The names come from include/rubraview/menu_identity.h and the extensions
from src/core/filemanage.c, so the viewer and the package cannot disagree.

usage: make-menu-package.py <version x.y.z> <output .msix>
"""

import base64
import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).absolute().parent.parent
BLOCK = 65536
LOGOS = ("Square150x150Logo.png", "Square44x44Logo.png", "StoreLogo.png")


def identity():
    text = (ROOT / "include" / "rubraview" / "menu_identity.h").read_text(encoding="utf-8")
    return dict(re.findall(r'#define RUBRAVIEW_MENU_(\w+) "([^"]*)"', text))


def extensions():
    text = (ROOT / "src" / "core" / "filemanage.c").read_text(encoding="utf-8")
    body = text[text.index("rubraview_shell_extensions_all(void)"):]
    return re.search(r'U8\("([^"]*)"\)', body).group(1).split(";")


def publisher_id(publisher):
    """The first 8 bytes of SHA-256 over the UTF-16LE text, 13 characters of base 32."""
    value = int.from_bytes(hashlib.sha256(publisher.encode("utf-16le")).digest()[:8], "big")
    letters = "0123456789abcdefghjkmnpqrstvwxyz"
    out = ""
    for i in range(13):
        shift = 59 - 5 * i
        out += letters[((value >> shift) if shift >= 0 else (value << -shift)) & 31]
    return out


def manifest(names, version):
    clsid = names["CLSID"]
    types = "".join(
        '            <desktop4:ItemType Type=".%s">\r\n'
        '              <desktop4:Verb Id="Rubraview" Clsid="%s" />\r\n'
        "            </desktop4:ItemType>\r\n" % (ext, clsid)
        for ext in extensions()
    )
    return (
        '<?xml version="1.0" encoding="utf-8"?>\r\n'
        '<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"\r\n'
        '         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"\r\n'
        '         xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10"\r\n'
        '         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"\r\n'
        '         xmlns:desktop4="http://schemas.microsoft.com/appx/manifest/desktop/windows10/4"\r\n'
        '         xmlns:com="http://schemas.microsoft.com/appx/manifest/com/windows10"\r\n'
        '         IgnorableNamespaces="uap uap10 rescap desktop4 com">\r\n'
        '  <Identity Name="%s" Publisher="%s" Version="%s.0" ProcessorArchitecture="x64" />\r\n'
        "  <Properties>\r\n"
        "    <DisplayName>Rubraview</DisplayName>\r\n"
        "    <PublisherDisplayName>rubidus-api</PublisherDisplayName>\r\n"
        "    <Logo>Assets\\StoreLogo.png</Logo>\r\n"
        "    <uap10:AllowExternalContent>true</uap10:AllowExternalContent>\r\n"
        "  </Properties>\r\n"
        '  <Resources>\r\n    <Resource Language="en-us" />\r\n  </Resources>\r\n'
        "  <Dependencies>\r\n"
        '    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.19041.0" MaxVersionTested="10.0.26100.0" />\r\n'
        "  </Dependencies>\r\n"
        "  <Capabilities>\r\n"
        '    <rescap:Capability Name="runFullTrust" />\r\n'
        '    <rescap:Capability Name="unvirtualizedResources" />\r\n'
        "  </Capabilities>\r\n"
        "  <Applications>\r\n"
        '    <Application Id="Menu" Executable="rubraview.exe" uap10:TrustLevel="mediumIL" uap10:RuntimeBehavior="win32App">\r\n'
        '      <uap:VisualElements AppListEntry="none" DisplayName="Rubraview" Description="Rubraview"'
        ' BackgroundColor="transparent" Square150x150Logo="Assets\\Square150x150Logo.png"'
        ' Square44x44Logo="Assets\\Square44x44Logo.png" />\r\n'
        "      <Extensions>\r\n"
        '        <desktop4:Extension Category="windows.fileExplorerContextMenus">\r\n'
        "          <desktop4:FileExplorerContextMenus>\r\n"
        "%s"
        "          </desktop4:FileExplorerContextMenus>\r\n"
        "        </desktop4:Extension>\r\n"
        '        <com:Extension Category="windows.comServer">\r\n'
        "          <com:ComServer>\r\n"
        '            <com:SurrogateServer DisplayName="Rubraview">\r\n'
        '              <com:Class Id="%s" Path="%s" ThreadingModel="STA" />\r\n'
        "            </com:SurrogateServer>\r\n"
        "          </com:ComServer>\r\n"
        "        </com:Extension>\r\n"
        "      </Extensions>\r\n"
        "    </Application>\r\n"
        "  </Applications>\r\n"
        "</Package>\r\n"
    ) % (names["PACKAGE_NAME"], names["PACKAGE_PUBLISHER"], version, types, clsid, names["DLL_FILE"])


def main():
    if len(sys.argv) != 3 or not re.fullmatch(r"\d+\.\d+\.\d+", sys.argv[1]):
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    version, out = sys.argv[1], Path(sys.argv[2])
    names = identity()
    if publisher_id(names["PACKAGE_PUBLISHER"]) != names["PACKAGE_PUBLISHER_ID"]:
        print("make-menu-package: menu_identity.h's publisher id is not the publisher's", file=sys.stderr)
        return 1

    files = [("Assets/" + logo, (ROOT / "resources" / "distribution" / "menu" / logo).read_bytes()) for logo in LOGOS]
    files.append(("AppxManifest.xml", manifest(names, version).encode("utf-8")))

    block_map = (
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>'
        '<BlockMap xmlns="http://schemas.microsoft.com/appx/2010/blockmap"'
        ' HashMethod="http://www.w3.org/2001/04/xmlenc#sha256">'
    )
    for name, data in files:
        # Stored, with no extra field: the local header is 30 bytes and the name.
        block_map += '<File Name="%s" Size="%d" LfhSize="%d">' % (name.replace("/", "\\"), len(data), 30 + len(name))
        for at in range(0, len(data), BLOCK):
            block_map += '<Block Hash="%s"/>' % base64.b64encode(hashlib.sha256(data[at:at + BLOCK]).digest()).decode()
        block_map += "</File>"
    block_map += "</BlockMap>"
    content_types = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="png" ContentType="image/png" />'
        '<Default Extension="xml" ContentType="application/vnd.ms-appx.manifest+xml" />'
        '<Override PartName="/AppxBlockMap.xml" ContentType="application/vnd.ms-appx.blockmap+xml" /></Types>'
    )
    files.append(("AppxBlockMap.xml", block_map.encode("utf-8")))
    files.append(("[Content_Types].xml", content_types.encode("utf-8")))

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as package:
        for name, data in files:
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_STORED
            entry.create_system = 0
            package.writestr(entry, data)
    print("make-menu-package: %s (%s %s, %d extensions)" % (out, names["PACKAGE_NAME"], version, len(extensions())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
