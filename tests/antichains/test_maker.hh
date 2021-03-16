#pragma once

#include <iostream>
#include <functional>
#include <map>
#include <type_traits>
#include <cxxabi.h>

template <typename SetType>
class test_t;

struct generic_test_t {
    virtual void operator () () {}
    virtual ~generic_test_t () = default;
};

using test_maker_t = std::function<void ()>;

static std::map<std::string, test_maker_t> test_makers;

template <typename... Types>
struct type_list;

template <template <typename Arg> typename... Types>
struct template_type_list;

static std::set<std::string> set_names, vector_names;

#define typestring(T)                                                   \
  ([] () { int _; return abi::__cxa_demangle (typeid(T).name (), 0, 0, &_); }) ()

namespace antichains {
  template <typename VecType>
  struct all {};
}
namespace vectors {
  template <typename Unit>
  struct all {};
}

// One set, one vector
template <template <typename V> typename SetType, typename VecType>
void register_maker (type_list<VecType>*, SetType<VecType>* = 0) {
  vector_names.insert (typestring (VecType));

  auto ts = typestring (SetType<VecType>);
  auto test = [ts] () {
    std::cout << "[--] running tests for " << ts << "\r";
    test_t<SetType<VecType>> () ();
    std::cout << "[OK]" << std::endl;
  };

  test_makers[ts] = test;

  for (auto&& ts : {
      typestring (antichains::all<VecType>),
      typestring (SetType<vectors::all<char>>),
      typestring (antichains::all<vectors::all<char>>)
    }) {
    auto prev = test_makers[ts];
    test_makers[ts] = [prev, test] () {
      if (prev) prev ();
      test ();
    };
  }
}

// One set, multiple vectors
template <template <typename V> typename SetType, typename VecType, typename... VecTypes>
void register_maker (type_list<VecType, VecTypes...>*, SetType<VecType>* = 0) {
  register_maker<SetType> ((type_list<VecType>*) 0);
  register_maker<SetType> ((type_list<VecTypes...>*) 0);
}

// Multiple sets, multiple vectors
template <template <typename V> typename SetType, template <typename V> typename... SetTypes, typename... VecTypes>
void register_maker (type_list<VecTypes...>* v, template_type_list<SetType, SetTypes...>* = 0) {
  set_names.insert (typestring (SetType<int>));
  register_maker<SetType> (v);
  register_maker<SetTypes...> (v);
}
