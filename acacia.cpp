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

#include <assert.h>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

using namespace std;

void usage(char* progName) {
    cout << progName << " [OPTIONS] [FILENAME]" << endl
         << endl;
}

int main(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            case '?':  // getopt found an invalid option
                return EXIT_FAILURE;
            default:
                assert(false);  // this should not be reachable
        }
    }

    // making sure we have at most 1 (positional) non-option
    if (argc - optind > 1) {
        fprintf(stderr, "Expected at most 1 positional arguments!\n");
        return EXIT_FAILURE;
    } else {
        // pass
    }
    return EXIT_SUCCESS;
}
