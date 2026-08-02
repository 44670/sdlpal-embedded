/* -*- mode: c; tab-width: 4; c-basic-offset: 4; c-file-style: "linux" -*- */
//
// Copyright (c) 2009-2011, Wei Mingzhi <whistler_wmz@users.sf.net>.
// Copyright (c) 2011-2026, SDLPAL development team.
// All rights reserved.
//
// This file is part of SDLPAL.
//
// SDLPAL is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 3
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef _DEFINES_H
#define _DEFINES_H

/*
 * Resource ownership is selected only by MEM_LEVEL1 or MEM_LEVEL2.  Keep
 * board wiring, presentation, and storage topology as orthogonal features.
 */
#if defined(PAL_CARDPUTER_EXTREME)
#error "PAL_CARDPUTER_EXTREME is retired; select MEM_LEVEL1 or MEM_LEVEL2"
#endif

#if defined(PAL_PAGED_EVENT_STATE)
#error "PAL_PAGED_EVENT_STATE is derived; do not define it in a build profile"
#endif

/*
 * The SRAM-only profile and the current SD-only Level2 profile persist the
 * complete event/scene state through the bounded TF pager.  CoreS3 SE remains
 * a Level2 resident-state target.
 */
#if defined(MEM_LEVEL1) || defined(PAL_STORAGE_SD_ONLY)
#define PAL_PAGED_EVENT_STATE 1
#endif

#ifndef PAL_LOCALIZATION_EXT
# define PAL_LOCALIZATION_EXT "slf"
#endif

#endif //_DEFINES_H
