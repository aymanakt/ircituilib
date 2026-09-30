/**
 * @file version.h
 * @brief Version macros, the stringification helpers, and the accessors the
 *        generated version.c defines for them.
 *
 * Private to the library: not installed.  The generated version.c is the
 * definition site of the accessors below; everything else calls through them
 * rather than reading a global.
 */
#ifndef IRCITUI_VERSION_H
#define IRCITUI_VERSION_H

#define IRCITUI_MAJOR     0
#define IRCITUI_MINOR     0
#define IRCITUI_PATCH     3
#define IRCITUI_INTERNAL  102

#define STRINGISE_NX(A) #A
#define STRINGISE(A) STRINGISE_NX(A)

#ifdef __cplusplus
extern "C" {
#endif

const char *IrcitUiLibVersion(void)          __attribute__((const));
const char *IrcitUiLibVersionMajor(void)     __attribute__((const));
const char *IrcitUiLibVersionMinor(void)     __attribute__((const));
const char *IrcitUiLibVersionPatch(void)     __attribute__((const));
const char *IrcitUiLibVersionInternal(void)  __attribute__((const));
const char *IrcitUiLibCompiledAt(void)       __attribute__((const));
const char *IrcitUiLibCompiledBy(void)       __attribute__((const));

#ifdef __cplusplus
}
#endif

#endif //IRCITUI_VERSION_H
