# A filter for Rubraview (RFC-0007)

`posterise.c` is a complete filter: copy `include/rubraview/plugin_filter.h` next to it and build a 64-bit DLL
whose name ends in `.rvf.dll`:

    x86_64-w64-mingw32-gcc -O2 -shared -I../../../include -o posterise.rvf.dll posterise.c

or with Visual C++ (`cl /LD /O2 /I..\..\..\include posterise.c /Fe:posterise.rvf.dll`). Put it in the plug-in
folder (Settings › Display › Plug-ins; `%LOCALAPPDATA%\Rubraview\plugins` unless another is chosen), turn
plug-ins on, and it is under Edit › Effects › Plug-ins and `--batch --plugin=posterise:6,1`.

The filter runs in a child process: it may not show windows of its own in this version, and a filter that
does not return when `progress` says cancel is ended five seconds later.
