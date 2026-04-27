/*
 * asc-font.h (stub)
 *
 * This header is intentionally provided as a no-op.
 *
 * In AppStream < 1.0.6, the public header
 *   appstream-compose.h -> asc-canvas.h
 * includes the private header "asc-font.h".
 *
 * That private header is not installed by default, and nothing in
 * the public API actually depends on its declarations.
 *
 * To allow building against older AppStream releases, we provide
 * this empty stub so the include resolves cleanly.
 *
 * Safe to remove once the minimum required AppStream is >= 1.0.6.
 */
#ifndef __ASC_FONT_H
#define __ASC_FONT_H
#endif
