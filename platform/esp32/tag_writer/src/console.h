// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file console.h
 * The tag writer's serial commands. The display's console (UART0) owns the
 * line input and hands each "tag <command>" line here; `help` lists them.
 */
#pragma once

namespace console {

/** Runs one command line (without the "tag " prefix). The caller holds the
 * reader (tagsvc::acquire) and runs this on a task with room for it. Multi-line
 * commands (loadimage, libadd) read their extra lines from UART0 directly. */
void run(char *line);

}  // namespace console
