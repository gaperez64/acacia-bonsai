#pragma once
namespace vectors {
  template <typename Vec>
  class simd_po_res {
    private:
      template<class T, class = void>
      struct has_sum : std::false_type {};

      template <class T>
      struct has_sum<T, std::void_t<decltype (std::declval<T> ().sum)>> : std::true_type {};

      const auto& data_of (const Vec& v) {
        if constexpr (std::is_pointer_v<decltype (v.data)>)
          return *v.data;
        else
          return v.data;
      }

    public:
      simd_po_res (const Vec& lhs, const Vec& rhs) : lhs {lhs}, rhs {rhs},
                                                     nsimds {data_of (lhs).size ()} {
        if constexpr (has_sum<Vec>::value) {
          bgeq = (lhs.sum >= rhs.sum);
          if (not bgeq)
            has_bgeq = true;

          bleq = (lhs.sum <= rhs.sum);
          if (not bleq)
            has_bleq = true;

          if (has_bgeq or has_bleq)
            return;
        }
        bgeq = true;
        bleq = true;
        for (up_to = 0; up_to < nsimds; ++up_to) {
          //auto diff = data_of (lhs)[up_to] - data_of (rhs)[up_to];
          //bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
          //bleq = bleq and (std::experimental::reduce (-diff, std::bit_or ()) >= 0);
          bgeq = bgeq and (std::experimental::all_of (data_of (lhs)[up_to] >= data_of (rhs)[up_to]));
          bleq = bleq and (std::experimental::all_of (data_of (lhs)[up_to] <= data_of (rhs)[up_to]));
          if (not bgeq)
            has_bgeq = true;
          if (not bleq)
            has_bleq = true;
          if (has_bgeq or has_bgeq)
            return;
        }
        has_bgeq = true;
        has_bleq = true;
      }

      inline bool geq () {
        if (has_bgeq)
          return bgeq;
        assert (has_bleq);
        has_bgeq = true;
        for (; up_to < nsimds; ++up_to) {
          //auto diff = data_of (lhs)[up_to] - data_of (rhs)[up_to];
          bgeq = bgeq and (std::experimental::all_of (data_of (lhs)[up_to] >= data_of (rhs)[up_to]));
          //bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
          if (not bgeq)
            break;
        }
        return bgeq;
      }

      inline bool leq () {
        if (has_bleq)
          return bleq;
        assert (has_bgeq);
        has_bleq = true;
        for (; up_to < nsimds; ++up_to) {
          bleq = bleq and (std::experimental::all_of (data_of (lhs)[up_to] <= data_of (rhs)[up_to]));
          //auto diff = data_of (rhs)[up_to] - data_of (lhs)[up_to];
          //bleq = bleq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
          if (not bleq)
            break;
        }
        return bleq;
      }

    private:
      const Vec& lhs;
      const Vec& rhs;
      const size_t nsimds;
      bool bgeq, bleq;
      bool has_bgeq = false,
        has_bleq = false;
      size_t up_to = 0;
  };
}
