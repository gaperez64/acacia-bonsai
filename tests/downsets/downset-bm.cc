// TODO : Sorting and Selection in Posets | SIAM Journal on Computing | Vol. 40, No. 3 | Society for Industrial and Applied Mathematics
// SORTING AND SELECTION IN POSETS
// Th. 4.5
// For vector_backed (insertion based) remove insert, and allow constructor on list
// insert only if non dominated, this would replace it
// dynamic data structure (vector vs kdtree) by gaperez
// bm on syntcomp

#include <cassert>
#include <span>
#include <memory>
#include <ostream>
#include <set>
#include <vector>
#include <string>
#include <type_traits>
#include <cxxabi.h>
#include <random>
#include <ranges>
#include <algorithm>

#include <getopt.h>

#include <spot/misc/timer.hh>

#undef NO_VERBOSE
#include <utils/verbose.hh>

#include <utils/vector_mm.hh>
#include "downsets.hh"
#include "vectors.hh"

#include "test_maker.hh"

#include <valgrind/callgrind.h>

using namespace std::literals;

#ifndef DIMENSION
# define DIMENSION 128 * 1024
#endif

int               utils::verbose = 0;
utils::voutstream utils::vout;

using test_value_type = uint8_t;

using result_t = std::map<std::string, double>;

static std::default_random_engine::result_type starting_seed = 0;

static std::map<std::string, size_t> params = {
  {"maxval", 12},
  {"build", 10240ul * 2},
  {"query", 10240ul * 4},
  {"transfer", 1000000},
  {"intersection", 256ul},
  {"union", 512ul * 20 },
  {"rounds", 1}
};

static std::mt19937 rand_gen;
static std::uniform_int_distribution<uint32_t> distrib;

template <typename T>
auto random_vector () {
  utils::vector_mm<T> v (DIMENSION);
  std::generate (v.begin(), v.end(), [&] () { return distrib (rand_gen); });
  return v;
}

template <class T, class = void>
struct has_insert : std::false_type {};

template <class T>
struct has_insert<T, std::void_t<decltype(std::declval<T>().insert (std::declval<typename T::value_type> ()))>> : std::true_type {};

struct {
    size_t t1_sz, t1_in, t1_out;
    size_t t2_sz;
    size_t t3_sz;
} test_chk = {};

#define chk(Field, Value) do {                  \
  if (Field == 0)                               \
    Field = Value;                              \
  else                                          \
    if (Field != Value) {                                               \
      verb_do (0, vout << "ERROR: " << #Field " != " #Value << std::endl); \
      abort ();                                                         \
    }                                                                   \
  } while (0)

template <typename SetType>
struct test_t : public generic_test<result_t> {
    using VType = typename SetType::value_type;
    using value_type = typename VType::value_type;

    SetType vec_to_set (std::vector<VType>&& v) {
      if constexpr (has_insert<SetType>::value) {
        SetType set (std::move (v[0]));
        for (size_t i = 1; i < v.size (); ++i)
          set.insert (std::move (v[i]));
        return set;
      }
      else
        return SetType (std::move (v));
    }

    std::vector<VType> test_vector (size_t nitems, ssize_t delta = 0) {
      rand_gen.seed (starting_seed);
      verb_do (3, vout << "Creating a vector of " << nitems << " elements... " << std::flush);
      std::vector<VType> elts;
      elts.reserve (nitems);
      for (size_t i = 0; i < nitems; ++i) {
        auto r = random_vector<value_type> ();
        for (auto&& x : r)
          if (x > 0)
            x += delta;
        elts.push_back (VType (std::move (r)));
      }
      verb_do (3, vout << "done.\n" << std::flush);
      return elts;
    }

    result_t operator() () {
      result_t res;

      auto ROUNDS = params["rounds"];
      if (params["build"]) {
        verb_do (1, vout << "Test 1: Insertion and membership query" << std::endl);
        spot::stopwatch sw;
        double buildtime = 0, querytime = 0, transfertime = 0;
        for (size_t rounds = 0; rounds < ROUNDS; ++rounds) {
          verb_do (2, vout << "Round " << rounds << std::endl);
          verb_do (2, vout << "BUILD..." << std::flush);
          auto vec1 = test_vector (params["build"]);
          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          auto set = vec_to_set (std::move (vec1));
          CALLGRIND_STOP_INSTRUMENTATION;
          buildtime += sw.stop ();
          verb_do (2, vout << " SIZE: " << set.size () << std::endl);
          chk (test_chk.t1_sz, set.size ());
          if (params["query"]) {
            verb_do (2, vout << "QUERY..." << std::flush);
            auto vec2 = test_vector (params["query"], -1);
            size_t in = 0, out = 0;
            sw.start ();
            CALLGRIND_START_INSTRUMENTATION;
            for (auto&& x : vec2)
              if (set.contains (x))
                ++in;
              else
                ++out;
            CALLGRIND_STOP_INSTRUMENTATION;
            querytime += sw.stop ();
            verb_do (2, vout << "... IN: " << in << " OUT: " << out << std::endl);
            chk (test_chk.t1_in, in);
            chk (test_chk.t1_out, out);
          }
          if (auto tr = params["transfer"]) {
            verb_do (2, vout << "TRANSFER..." << std::flush);
            sw.start ();
            CALLGRIND_START_INSTRUMENTATION;
            for (size_t i = 0; i < tr; ++i) {
              auto set2 (std::move (set));
              chk (test_chk.t1_sz, set2.size ());
              set = std::move (set2);
              chk (test_chk.t1_sz, set.size ());
            }
            CALLGRIND_STOP_INSTRUMENTATION;
            transfertime += sw.stop ();
            verb_do (2, vout << " done." << std::endl);
            chk (test_chk.t1_sz, set.size ());
          }
        }

        res["build"] = buildtime;
        res["query"] = querytime;
        res["transfer"] = transfertime;

        verb_do (1, vout << "BUILD: " << buildtime / ROUNDS
                 << ", QUERY: " << querytime / ROUNDS
                 << ", TRANSFER: " << transfertime / ROUNDS
                 << std::endl);
      }

      if (const size_t NITEMS = params["intersection"]) {
        verb_do (1, vout << "Test 2: Intersections" << std::endl);
        spot::stopwatch sw;
        double intertime = 0;
        for (size_t rounds = 0; rounds < ROUNDS; ++rounds) {
          verb_do (2, vout << "Round " << rounds << std::endl);
          verb_do (2, vout << "BUILD\n");
          auto set = vec_to_set (test_vector (NITEMS));
          auto vec = test_vector (2 * NITEMS);
          decltype (vec) vec2;
          vec2.insert (vec2.end (), std::make_move_iterator (vec.begin () + NITEMS / 2),
                       std::make_move_iterator (vec.begin () + 3 * NITEMS / 2));
          auto set2 = vec_to_set (std::move (vec2));
          verb_do (2, vout << "INTERSECT...";);
          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          set.intersect_with (std::move (set2));
          CALLGRIND_STOP_INSTRUMENTATION;
          intertime += sw.stop ();
          verb_do (2, vout << " SIZE: " << set.size () << std::endl);
          chk (test_chk.t2_sz, set.size ());
        }
        res["intersection"] = intertime;
        verb_do (1, vout << "INTER: " << intertime / ROUNDS
                 << std::endl);
      }


      if (const size_t NITEMS = params["union"]) {
        verb_do (1, vout << "Test 3: Unions" << std::endl);
        spot::stopwatch sw;
        double uniontime = 0;
        for (size_t rounds = 0; rounds < ROUNDS; ++rounds) {
          verb_do (2, vout << "Round " << rounds << std::endl);

          verb_do (2, vout << "BUILD\n");
          auto set = vec_to_set (test_vector (NITEMS));
          auto vec = test_vector (2 * NITEMS);
          decltype (vec) vec2;
          vec2.insert (vec2.end (), std::make_move_iterator (vec.begin () + NITEMS / 2),
                       std::make_move_iterator (vec.begin () + 3 * NITEMS / 2));
          auto set2 = vec_to_set (std::move (vec2));
          verb_do (2, vout << "UNION...";);
          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          set.union_with (std::move (set2));
          CALLGRIND_STOP_INSTRUMENTATION;
          uniontime += sw.stop ();
          verb_do (2, vout << " SIZE: " << set.size () << std::endl);
          chk (test_chk.t3_sz, set.size ());
        }
        res["union"] = uniontime;
        verb_do (1, vout << "UNION: " << uniontime / ROUNDS
                 << std::endl);
      }

      return res;
    }
};

namespace vectors{
  template <typename T>
  using array_backed_fixed = vectors::array_backed<T, DIMENSION>;

  template <typename T>
  using array_ptr_backed_fixed = vectors::array_ptr_backed<T, DIMENSION>;

  template <typename T>
  using simd_array_backed_fixed = vectors::simd_array_backed<T, DIMENSION>;

  template <typename T>
  using simd_array_ptr_backed_fixed = vectors::simd_array_ptr_backed<T, DIMENSION>;
}

using vector_types = type_list<
  vectors::array_backed_fixed<test_value_type>,
  vectors::array_ptr_backed_fixed<test_value_type>,
  vectors::simd_array_backed_fixed<test_value_type>,
  vectors::simd_array_ptr_backed_fixed<test_value_type>,
  vectors::vector_backed<test_value_type>,
  vectors::simd_vector_backed<test_value_type>>;

using set_types = template_type_list<
  downsets::kdtree_backed,
  downsets::vector_or_kdtree_backed,
  downsets::vector_backed,
  downsets::vector_backed_bin,
  downsets::vector_backed_one_dim_split>;

void usage (const char* progname) {
  std::cout << "usage: " << progname << " [-v -v -v...] [--params PARAMS] [--seed N] SETTYPE VECTYPE" << std::endl;

  std::cout << "List of parameters:" << std::endl;
  for (auto& [p, v] : params)
    std::cout << "  " << p << " (default: " << v << ")" << std::endl;
  std::cout << std::endl;
  std::cout << "  SETTYPE:\n  - all" << std::endl;
  for (auto&& s : set_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('<') - start) << std::endl;
  }

  std::cout << "  VECTYPE:\n  - all" << std::endl;
  for (auto&& s : vector_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('>') - start + 1) << std::endl;
  }

  exit (0);
}

int main (int argc, char* argv[]) {
  const char* prog = argv[0];

  register_maker<false> ((vector_types*) 0, (set_types*) 0);

  while (true) {
    static struct option long_options[] = {
      {"seed",    required_argument, 0,  's' },
      {"verbose", no_argument,       0,  'v' },
      {"params",  required_argument, 0,  'p' },
      {0,         0,                 0,  0 }
    };

    int c = getopt_long (argc, argv, "s:vp:", long_options, NULL);
    if (c == -1)
      break;
    switch (c) {
      case 's':
        starting_seed = std::strtoul (optarg, nullptr, 10);
        break;
      case 'v':
        ++utils::verbose;
        break;
      case 'p':
        while (optarg) {
          char* comma = strchr (optarg, ',');
          if (comma) *comma = '\0';
          char* equal = strchr (optarg, '=');
          if (not equal) {
            std::cerr << "invalid parameter setting: " << optarg << std::endl;
            usage (prog);
          }
          *equal = '\0';
          std::string p {optarg}, arg {equal + 1};
          auto pk = std::views::keys (params);
          if (std::ranges::find (pk, p) == std::ranges::end (pk)) {
            std::cerr << "unknown parameter: " << p << std::endl;
            usage (prog);
          }
          params[p] = std::stoul (arg);
          optarg = comma ? comma + 1 : NULL;
        }
        break;
    }
  }

  decltype (distrib)::param_type distrib_params (0, (int32_t) params["maxval"]);
  distrib.param (distrib_params);

  if (argc - optind != 2)
    usage (prog);

  auto downarg = std::string {argv[optind]},
    vecarg = std::string {argv[optind + 1]};

  std::list<std::string> downs, vecs;
  auto& tests = test_list<result_t>::list;
  auto to_string_view = [] (auto&& str) {
    return std::string_view(&*str.begin(), std::ranges::distance(str));
  };

  for (auto&& da : downarg
         | std::ranges::views::split(',')
         | std::ranges::views::transform (to_string_view))
    for (auto& k : std::views::keys (tests))
      if (da == "all" or k.starts_with ("downsets::"s + std::string (da))) {
        auto sub = k.substr (0, k.find ("<"));
        if (std::ranges::find (downs.begin (), downs.end (), sub) == downs.end ())
          downs.push_back (sub);
      }

  for (auto&& va : vecarg
         | std::ranges::views::split(',')
         | std::ranges::views::transform (to_string_view))
    for (auto& k : std::views::keys (tests)) {
      auto v = k.substr (k.find ("<"));
      if (va == "all" or v.starts_with ("<vectors::"s + std::string (va)))
        if (std::ranges::find (vecs.begin (), vecs.end (), v) == vecs.end ())
          vecs.push_back (v);
    }

  if (downs.empty () or vecs.empty ()) {
    std::cout << "error: no such implementation." << std::endl;
    return 1;
  }

  std::map<std::string, result_t> all_res;
  for (auto& ds : downs)
    for (auto& v : vecs) {
      vectors::bool_threshold = DIMENSION;
      vectors::bitset_threshold = DIMENSION;
      all_res[ds + v] = tests[ds + v] ();
    }
  for (auto& res : all_res) {
    std::cout << "- " << res.first << ":\n";
    for (auto& test : res.second)
      std::cout << "  - " << test.first << ": " << test.second << "\n";
  }
  return 0;
}
