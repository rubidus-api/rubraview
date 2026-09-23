#ifndef RUBRAVIEW_VERSION_H
#define RUBRAVIEW_VERSION_H

/*
 * The version, in one place.
 *
 * Everything else derives from this line: the `Makefile` reads it with
 * sed to name the executable, the packaging script stamps it into the
 * release folder, and the program reports it. Writing it twice is how a
 * binary ends up claiming one version while its folder claims another,
 * and then nobody can say which build a bug report came from.
 *
 * When bumping it, change the string; the numbers below are for code
 * that needs to compare versions rather than print them, and the gate
 * checks that the two agree.
 */

#define RUBRAVIEW_VERSION_STRING "0.0.13"

#define RUBRAVIEW_VERSION_MAJOR 0
#define RUBRAVIEW_VERSION_MINOR 0
#define RUBRAVIEW_VERSION_PATCH 13

#endif /* RUBRAVIEW_VERSION_H */
