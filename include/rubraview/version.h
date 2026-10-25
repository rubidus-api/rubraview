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

#define RUBRAVIEW_VERSION_STRING "1.39.25"

#define RUBRAVIEW_VERSION_MAJOR 1
#define RUBRAVIEW_VERSION_MINOR 39
#define RUBRAVIEW_VERSION_PATCH 25

/* The day this version was released, for the help window (owner, 2026-10-18). */
#define RUBRAVIEW_RELEASE_DATE "2026-10-24"
#define RUBRAVIEW_PROJECT_URL  "https://github.com/rubidus-api/rubraview"

#endif /* RUBRAVIEW_VERSION_H */
