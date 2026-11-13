/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org> */
#ifndef SIXEL_HOSTCURSOR_H
#define SIXEL_HOSTCURSOR_H
int sixelHostCursorInit(void);
void sixelHostCursorDispatch(void);
void sixelHostCursorFini(void);
#endif
