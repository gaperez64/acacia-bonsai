// Test oracle only, adapted from vanessas-oink 872be945, src/solvers/strpm.cpp
// (Strahler-tree progress measures by V. Flügel and G. A. Pérez), relicensed by its
// authors under this project's GPL-3.0.
#pragma once
#include <unordered_set>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
namespace parity_test {
  inline int count_distinct_nondecreasing (std::vector<int>::const_iterator first,
                                           std::vector<int>::const_iterator last) {
    if (first == last)
      return 0;
    int count = 1;
    for (auto it = first + 1; it != last; ++it)
      count += *it != *(it - 1);
    return count;
  }
  struct scalar_lifting {
      int k, t, h;
      std::vector<uint8_t> tmp_b;
      std::vector<int> tmp_d;
      int skipUntilNextLevel (std::vector<int>& curr_d, int i);
      void prog_tmp (int pindex, int height);
      int compare (int pindex, std::vector<uint8_t>& other_b, std::vector<int>& other_d);
  };
  int scalar_lifting::skipUntilNextLevel (std::vector<int>& curr_d, int i) {
    while ((i == curr_d.size () - 1) || (i >= 0 && curr_d[i] == curr_d[i + 1])) {
      tmp_b[i] = 0;
      i--;
    }
    return i;
  }

  /**
   * Set tmp := min { m | m >_p tmp }
   */
  void scalar_lifting::prog_tmp (int pindex, int h) {
    if (tmp_d[0] == -1)
      return;

    bool skipLevel = false;
    int i = tmp_d.size () - 1;
    assert (*std::max_element (tmp_d.begin (), tmp_d.end ()) < h - 1);

    while (i >= 0 && tmp_d[i] > pindex) {
      tmp_b[i] = 0;
      i--;
    }

    int nes = count_distinct_nondecreasing (tmp_d.begin (), tmp_d.begin () + i + 1);
    assert (nes >= 0);

    if (nes - 1 == k - 1)

      i = skipUntilNextLevel (tmp_d, i);

    while (i >= 0) {
      if (i == tmp_d.size () - 1 or tmp_d[i] != tmp_d[i + 1]) {
        int nlb = (i + 1) - nes;
        nes--;
        if (nlb < t) {
          int new_index = std::min (
              std::cmp_equal (i + 1, tmp_d.size ()) ? tmp_d[i] : tmp_d[i + 1] - 1, pindex);
          assert (new_index < h - 1);
          i++;

          bool isNewString = false;
          if (i != 0 and (i == tmp_d.size () or tmp_d[i - 1] == new_index))
            nlb++;
          else {
            nes++;
            isNewString = true;
          }

          int total_nes = std::min ((nes + h - 1 - new_index), k - 1);
          if (std::cmp_greater (total_nes + t, tmp_b.size ())) {
            size_t newBits = total_nes + t - tmp_d.size ();

            tmp_b.insert (tmp_b.end (), newBits, 0);
            tmp_b[i] = 1;
            for (size_t j = i; j < tmp_d.size (); j++)
              tmp_d[j] = new_index;
            tmp_d.insert (tmp_d.end (), newBits, new_index);
            i += t - nlb + 1;
          }
          else {
            tmp_b[i] = 1;
            tmp_d[i] = new_index;

            int j = 1;
            while (nlb + j <= t) {
              tmp_b[i + j] = 0;
              tmp_d[i + j] = new_index;
              j++;
            }

            i += j;
          }
          break;
        }

        else if (i == 0 or tmp_d[i - 1] != tmp_d[i]) {
          tmp_b[i] = 0;
          i--;
          continue;
        }
      }

      if (tmp_b[i] == 0) {
        if (i == 0 || tmp_d[i - 1] != tmp_d[i]) {
          int strings_after_current =
              count_distinct_nondecreasing (tmp_d.begin () + i, tmp_d.end ());
          if (strings_after_current == (h - 1) - tmp_d[i]) {
            i--;
            continue;
          }
          else {
            nes--;
            skipLevel = true;
          }

          break;
        }
        else {
          assert (tmp_d[i - 1] == tmp_d[i]);
          break;
        }
      }
      else {
        tmp_b[i] = 0;
        i--;
      }
    }

    if (i == -1) {
      if (tmp_d[0] == 0) {
        tmp_d[0] = -1;
        return;
      }
      else {
        i = 0;
        tmp_b[i] = 1;

        tmp_d[i] = std::min (tmp_d[i], pindex + 1) - 2;
        if (std::cmp_greater (t + k - 1, tmp_b.size ())) {
          size_t newBits = t + k - 1 - tmp_d.size ();
          tmp_b.insert (tmp_b.end (), newBits, 0);
          for (size_t j = i; j < tmp_d.size (); j++)
            tmp_d[j] = tmp_d[i];
          tmp_d.insert (tmp_d.end (), newBits, tmp_d[i]);
        }
        skipLevel = true;
        nes--;
      }
    }

    int available_nes = h - 1 - (tmp_d[(skipLevel ? i : i - 1)] + 1);
    int no_of_needed_nes = std::min ((k - 1) - (nes + 1), available_nes);

    if (no_of_needed_nes == 0) {
      tmp_d.resize (i);
      tmp_b.resize (i);
    }
    else {
      int set_index = tmp_d[(skipLevel ? i : i - 1)] + 1;

      while (std::cmp_greater_equal (tmp_b.size (), i + no_of_needed_nes)) {
        assert (i >= 0 and std::cmp_less (i, tmp_d.size ()));
        assert (set_index < h - 1);
        tmp_d[i] = set_index;
        i++;
      }

      while (std::cmp_less (i, tmp_b.size ())) {
        set_index++;
        assert (set_index < h - 1);
        tmp_d[i] = set_index;
        i++;
      }
    }

    assert ((tmp_d.size () - std::unordered_set<int> (tmp_d.begin (), tmp_d.end ()).size ()) <= t);
  }

  int scalar_lifting::compare (int pindex, std::vector<uint8_t>& other_b,
                               std::vector<int>& other_d) {
    if (tmp_d[0] == -1 and other_d[0] == -1)
      return 0;
    if (tmp_d[0] == -1)
      return 1;
    if (other_d[0] == -1)
      return -1;

    for (int i = 0; i < std::max (tmp_d.size (), other_d.size ()); i++)
      if (i >= other_b.size ())
        return tmp_b[i] == 0 ? -1 : 1;
      else if (i >= tmp_b.size ())
        return other_b[i] == 0 ? 1 : -1;
      else if (tmp_d[i] > pindex and other_d[i] > pindex)

        return 0;
      else if (tmp_d[i] < other_d[i])

        return tmp_b[i] == 0 ? -1 : 1;
      else if (tmp_d[i] > other_d[i])

        return other_b[i] == 0 ? 1 : -1;
      else if (tmp_b[i] < other_b[i])

        return -1;
      else if (tmp_b[i] > other_b[i])

        return 1;
    return 0;
  }

}
