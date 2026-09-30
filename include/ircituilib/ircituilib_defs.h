/**
 * @file ircituilib_defs.h
 * @brief Per-library linkage markers for ircituilib.
 *
 * Colocated with the library's public headers, one per library, per the
 * project convention: `PUBLIC_API` is defined here and nowhere else, so a
 * shared-library build can change the linkage of every public function in one
 * place without editing a single declaration.
 */
#ifndef IRCITUI_IRCITUILIB_DEFS_H
#define IRCITUI_IRCITUILIB_DEFS_H

/**
 * @brief Marks a function as part of this library's public contract.
 *
 * Empty for the static build. It is the hook for
 * `__attribute__((visibility("default")))` when ircituilib is built as a shared
 * object and the default symbol visibility is narrowed.
 *
 * Never applied to static functions (they use the `s` prefix instead),
 * typedefs, enums, or macros.
 */
#define PUBLIC_API

#endif /* IRCITUI_IRCITUILIB_DEFS_H */
