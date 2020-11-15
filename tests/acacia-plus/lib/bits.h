/*
 * This file is part of Acacia+, a tool for synthesis of reactive systems using antichain-based techniques
 * Copyright (C) 2011-2013 UMONS-ULB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BIT_H
#define BIT_H

#include <stdio.h>

/** Macro functions **/
#define BIT(bit) (1UL<<(bit))

/** Constants **/
#define LABEL_BIT_REPRES unsigned long //labels are represented by vectors of that type of integer
#define LABEL_BIT_RANGE 64 //the size of LABEL_BIT_REPRES (even number)

/** Functions prototypes **/
LABEL_BIT_REPRES build_int(int, int);
void print_bits(LABEL_BIT_REPRES);

#endif
