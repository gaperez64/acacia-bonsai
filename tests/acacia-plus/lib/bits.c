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

#include "bits.h"

/** Builds a new integer of type LABEL_BIT_REPRES and value 2^(bit_from-0)+...+2^(bit_from-nb_bits-1) **/
LABEL_BIT_REPRES
build_int(int bit_from, int nb_bits) {
	LABEL_BIT_REPRES new_int = 0;
	int i;
	for(i=0; i<nb_bits; i++) {
		new_int += BIT(bit_from-i);
	}
	return new_int;
}

/** Prints n in base 2 **/
void
print_bits(LABEL_BIT_REPRES n) {
	int i;
	for(i=LABEL_BIT_RANGE-1; i>=0; i--) {
		printf("%lu", (n>>i) & 1);
	}
}
