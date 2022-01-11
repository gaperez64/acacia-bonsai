#pragma once
#include <map>
#include <memory>

namespace utils {
  template <typename Ret, typename... Args>
  class cache_t {
      using cache_key_t = std::tuple<Args...>;
      using cache_map_t = std::map <cache_key_t, Ret>;
    public:
      cache_t () {}

      auto get (const Args&... args) const {
        auto cached = cache.find (std::make_tuple (args...));
        return cached != cache.end () ? &cached->second : nullptr;
      }

      const Ret& operator () (const Ret& r, const Args&... args) {
        return (cache.insert_or_assign (std::make_tuple (args...), r).first)->second;
      }
    private:
      cache_map_t cache;
  };

  template <typename Ret, typename... Args>
  auto make_cache (const Args&...) {
    return cache_t<Ret, Args...> ();
  }

  template <typename Ret, typename... Args>
  auto make_cache () {
    return cache_t<Ret, Args...> ();
  }
}
