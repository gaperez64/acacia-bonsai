/**************************************************************************
 * Copyright (c) 2020- Guillermo A. Perez
 * Copyright (c) 2020- Michael Cadilhac
 * 
 * This file is part of Acacia bonsai.
 * 
 * Acacia bonsai is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Acacia bonsai is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Acacia bonsai. If not, see <http://www.gnu.org/licenses/>.
 * 
 * Guillermo A. Perez
 * University of Antwerp
 * guillermoalberto.perez@uantwerpen.be
 *************************************************************************/

#ifndef _ANTICHAIN_H
#define _ANTICHAIN_H

class Antichain {
    public:
        virtual ~Antichain(){}
        virtual void shift(int) = 0;
        virtual Antichain unionWith(const Antichain&) = 0;
        virtual Antichain intersectWith(const Antichain&) = 0;
        Antichain operator|(const Antichain& a) {
            return this->unionWith(a);
        }
        Antichain operator&(const Antichain& a) {
            return this->intersectWith(a);
        }
};

#endif
