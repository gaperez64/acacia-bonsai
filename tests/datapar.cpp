/*  This file is part of the Vc library. {{{
Copyright © 2009-2016 Matthias Kretz <kretz@kde.org>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the names of contributing organizations nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

}}}*/

#define WITH_DATAPAR 1
#include "unittest.h"
#include <Vc/datapar>

// all_test_types / ALL_TYPES {{{1
typedef expand_list<
    Typelist<
    /*
#ifdef Vc_HAVE_FULL_AVX_ABI
        Template<Vc::datapar, Vc::datapar_abi::avx>,
#endif
#ifdef Vc_HAVE_FULL_SSE_ABI
        Template<Vc::datapar, Vc::datapar_abi::sse>,
#endif
*/
        Template<Vc::datapar, Vc::datapar_abi::scalar>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<2>>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<3>>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<4>>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<8>>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<12>>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<16>>,
        Template<Vc::datapar, Vc::datapar_abi::fixed_size<32>>>,
    Typelist<long double, double, float, unsigned long, int, unsigned short, signed char,
             long, unsigned int, short, unsigned char>> all_test_types;

#define ALL_TYPES (all_test_types)

// datapar generator function {{{1
template <class V> V make_vec(const std::initializer_list<typename V::value_type> &init)
{
    std::size_t i = 0;
    V r;
    for (;;) {
        for (auto x : init) {
            r[i] = x;
            if (++i == V::size()) {
                return r;
            }
        }
    }
}

TEST_TYPES(V, broadcast, ALL_TYPES)  //{{{1
{
    using T = typename V::value_type;
    VERIFY(Vc::is_datapar_v<V>);

    {
        V x;      // default broadcasts 0
        x = V{};  // default broadcasts 0
        x = V();  // default broadcasts 0
        x = x;
        for (std::size_t i = 0; i < V::size(); ++i) {
            COMPARE(x[i], T(0));
        }
    }

    V x = 1;
    V y = 0;
    for (std::size_t i = 0; i < V::size(); ++i) {
        COMPARE(x[i], T(1));
        COMPARE(y[i], T(0));
    }
    y = 1;
    COMPARE(x, y);
}

TEST_TYPES(V, operators, ALL_TYPES)  //{{{1
{
    using M = typename V::mask_type;
    using T = typename V::value_type;
    {  // compares{{{2
        V x = 1, y = 0;
        static_assert(std::is_same<decltype(x == x), M>::value, "");
        VERIFY(all_of(x == x));
        VERIFY(all_of(x != y));
        VERIFY(all_of(y != x));
        VERIFY(none_of(x != x));
        VERIFY(none_of(x == y));
        VERIFY(none_of(y == x));
        VERIFY(all_of(x > y));
        VERIFY(all_of(x >= y));
        VERIFY(all_of(x >= x));
        VERIFY(all_of(x <= x));
        VERIFY(none_of(x < y));
        VERIFY(none_of(x <= y));
    }
    {  // subscripting{{{2
        V x = 1;
        for (std::size_t i = 0; i < V::size(); ++i) {
            COMPARE(x[i], T(1));
            x[i] = 0;
        }
        COMPARE(x, V{0});
        for (std::size_t i = 0; i < V::size(); ++i) {
            COMPARE(x[i], T(0));
            x[i] = 1;
        }
        COMPARE(x, V{1});
    }
    {  // negation{{{2
        V x = 0;
        COMPARE(!x, M{true});
        V y = 1;
        COMPARE(!y, M{false});
    }
}

// vim: foldmethod=marker