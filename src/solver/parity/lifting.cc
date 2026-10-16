// Adapted from vanessas-oink 872be945, src/solvers/strpm_simd.{hpp,cpp}
// (Strahler-tree progress measures by V. Flügel and G. A. Pérez), relicensed by its
// authors under this project's GPL-3.0.

#include "lanes.hh"

namespace acacia::parity::detail {
  void lifting::prog_tmp (int pindex, int h) {
    if (k == 1 and tmp_nlanes == 0) {
      tmp_levels[0] = TOP_SENTINEL;
      tmp_nlanes = 1;
      tmp_bits = 0;
      tmp_masks = 0;
      fill_inactive_tmp ();
      return;
    }

    if (tmp_levels[0] == TOP_SENTINEL)
      return;

    lane_mask has_bits = (tmp_masks > 0);
    lanes per_elem = simd_popcount_u16x8 (tmp_masks);
    where (has_bits, per_elem) = per_elem - lanes (1);

    lanes nlb_counts = simd_prefix_sum_inclusive_u16x8 (per_elem);

    lanes nlb_before = nlb_counts - per_elem;

    lane_mask stp_mask = (tmp_levels <= lanes (static_cast<uint16_t> (pindex)));

    lanes expected_afa =
        lanes (static_cast<uint16_t> (h - 1 - static_cast<int> (tmp_nlanes))) + LANE_INDICES;
    lane_mask afa_mask = (tmp_levels == expected_afa);

    lane_mask smaller_than_p = has_bits and stp_mask;

    lanes clear_first_bit (static_cast<uint16_t> (0xFFFE));
    lanes pattern_zero_and_ones = tmp_masks & clear_first_bit;

    lanes t16 (static_cast<uint16_t> (t));
    lane_mask no_successor =
        has_bits and
        ((nlb_before == t16) or
         ((nlb_counts == t16) and
          (((tmp_bits == pattern_zero_and_ones) and afa_mask) or (tmp_bits == tmp_masks))));

    lane_mask has_successor = smaller_than_p and has_bits and !no_successor;

    int match = find_last_set (has_successor);
    if (match == -1) {
      if (tmp_levels[0] == 0) {
        tmp_nlanes = 1;
        tmp_levels[0] = TOP_SENTINEL;
        fill_inactive_tmp ();
        return;
      }
      else {
        tmp_bits[0] = 1;
        match = 0;
        tmp_levels[0] =
            static_cast<uint16_t> (std::min (static_cast<int> (tmp_levels[0]) - 1, pindex));
      }
    }
    else if (nlb_counts[match] == t) {
      int reset_bits =
          std::countl_one (static_cast<uint16_t> (tmp_bits[match] | ~tmp_masks[match])) - 8 + 1;
      int current_bits = std::popcount (static_cast<uint16_t> (tmp_masks[match]));

      tmp_bits[match] &= (1u << (8 - reset_bits)) - 1;
      tmp_masks[match] &= (1u << (8 - reset_bits)) - 1;
      if (reset_bits < 8) {
        nlb_counts[match] -=
            current_bits - std::popcount (static_cast<uint16_t> (tmp_masks[match]));
        match++;
        if (match < k - 1) {
          tmp_levels[match] = tmp_levels[match - 1] + 1;
          tmp_bits[match] = 0;
        }
      }
      else {
        nlb_counts[match] -= (current_bits - 1);
        if ((match + 1 == tmp_nlanes and tmp_levels[match] < h - 2) or
            (match + 1 < tmp_nlanes and tmp_levels[match] + 1 < tmp_levels[match + 1])) {
          tmp_levels[match] =
              (match + 1 == tmp_nlanes) ? tmp_levels[match] + 1 : tmp_levels[match + 1] - 1;
        }
        else
          tmp_levels[match] = tmp_levels[match] + 1;
      }
    }
    else

        if ((match + 1 < tmp_nlanes and tmp_levels[match] + 1 < tmp_levels[match + 1] and
             tmp_levels[match] != pindex)) {
      match++;
      tmp_bits[match] = 1;
      tmp_levels[match] =
          static_cast<uint16_t> (std::min (pindex, static_cast<int> (tmp_levels[match]) - 1));
    }
    else {
      int first_new = std::popcount (static_cast<uint16_t> (tmp_masks[match]));
      tmp_bits[match] |= (1u << first_new);
    }
    if (match < k - 1) {
      int bits_before = match > 0 ? nlb_counts[match - 1] : 0;
      tmp_masks[match] = (1u << (t - bits_before + 1)) - 1;

      const int set_to_level = static_cast<int> (tmp_levels[match]) + 1;

      const int max_fill =
          std::max (0, std::min (k - 2 - match, h - 2 - static_cast<int> (tmp_levels[match])));
      tmp_nlanes = static_cast<uint8_t> (match + 1 + max_fill);
      if (max_fill > 0) {
        const lanes fill_lev =
            lanes (static_cast<uint16_t> (set_to_level - (match + 1))) + LANE_INDICES;

        const lane_mask in_range =
            (LANE_INDICES >= lanes (static_cast<uint16_t> (match + 1))) and
            (LANE_INDICES < lanes (static_cast<uint16_t> (match + 1 + max_fill)));
        where (in_range, tmp_levels) = fill_lev;
      }

      lane_mask after_match_mask = LANE_INDICES > lanes (static_cast<uint16_t> (match));
      lane_mask before_levels_mask = LANE_INDICES < lanes (tmp_nlanes);
      where (before_levels_mask and after_match_mask, tmp_masks) = lanes (1);
      where (after_match_mask, tmp_bits) = lanes (0);

      fill_inactive_tmp ();
    }
  }

  int lifting::compare (int pindex) {
    if (pindex < 0) {
      const bool a_top = tmp_nlanes && tmp_levels[0] == TOP_SENTINEL;
      const bool b_top = best_nlanes && best_levels[0] == TOP_SENTINEL;
      return int (a_top) - int (b_top);
    }
    if (k == 1) {
      if (tmp_nlanes == best_nlanes)
        return 0;
      else if (tmp_nlanes > 0)
        return 1;
      else if (best_nlanes > 0)
        return -1;
    }

    if (tmp_levels[0] == TOP_SENTINEL and best_levels[0] == TOP_SENTINEL)
      return 0;
    if (tmp_levels[0] == TOP_SENTINEL)
      return 1;
    if (best_levels[0] == TOP_SENTINEL)
      return -1;

    if (k == 2) {
      if (tmp_nlanes == 0 and best_nlanes == 0)
        return 0;
      if (tmp_nlanes > best_nlanes)
        return (tmp_bits[0] & 1) == 0 ? -1 : 1;
      if (best_nlanes > tmp_nlanes)
        return (best_bits[0] & 1) == 0 ? 1 : -1;

      int tl = tmp_levels[0], bl = best_levels[0];
      if (tl > pindex and bl > pindex)
        return 0;
      if ((tl <= pindex and bl > pindex) or (tl < bl))
        return (tmp_bits[0] & 1) == 0 ? -1 : 1;
      if ((tl > pindex and bl <= pindex) or (tl > bl))
        return (best_bits[0] & 1) == 0 ? 1 : -1;

      uint16_t tm = tmp_masks[0], bm = best_masks[0];
      uint16_t tb = tmp_bits[0], bb = best_bits[0];
      if (tm == bm and tb == bb)
        return 0;

      uint16_t shorter = tm & bm;
      uint16_t d = tm ^ bm;
      uint16_t fld = d & ~(d << 1);
      uint16_t bxor = tb ^ bb;
      uint16_t comb = shorter + fld;
      uint16_t rxor = bxor & comb;

      bool al = (tm < bm and rxor == fld) or (tm > bm and rxor == 0);
      bool bl2 = (bm < tm and rxor == fld) or (bm > tm and rxor == 0);

      uint16_t dbits = shorter & bxor;
      uint16_t fbd = dbits & (uint16_t (0) - dbits);
      bool ag = (dbits > 0) and ((tb & fbd) > 0);
      bool bg = (dbits > 0) and ((bb & fbd) > 0);

      if (ag or (!al and bl2))
        return 1;
      if (bg or (al and !bl2))
        return -1;
      return 0;
    }

    lanes shorter_string = tmp_masks & best_masks;
    lanes diff = tmp_masks ^ best_masks;
    lanes first_length_difference = diff & ~(diff << 1);
    lanes bit_xor = tmp_bits ^ best_bits;

    lanes combined = shorter_string + first_length_difference;
    lanes relevant_xor = bit_xor & combined;

    lane_mask a_less = ((tmp_masks < best_masks) and (relevant_xor == first_length_difference)) or
                       ((tmp_masks > best_masks) and (relevant_xor == lanes (0)));
    lane_mask b_less = ((best_masks < tmp_masks) and (relevant_xor == first_length_difference)) or
                       ((best_masks > tmp_masks) and (relevant_xor == lanes (0)));

    lanes different_bits = (shorter_string & bit_xor);
    lanes first_bit_difference = different_bits & (lanes (0) - different_bits);
    lane_mask a_greater =
        (different_bits > lanes (0)) and ((tmp_bits & first_bit_difference) > lanes (0));
    lane_mask b_greater =
        (different_bits > lanes (0)) and ((best_bits & first_bit_difference) > lanes (0));

    const bool pindex_negative = (pindex < 0);
    lanes pi (static_cast<uint16_t> (std::max (pindex, 0)));

    lane_mask in_tmp = LANE_INDICES < lanes (tmp_nlanes);
    lane_mask in_best = LANE_INDICES < lanes (best_nlanes);
    lane_mask both_active = in_tmp and in_best;

    lane_mask tmp_extra = in_tmp and !in_best;
    lane_mask best_extra = in_best and !in_tmp;

    lane_mask tl_earlier = pindex_negative
                               ? lane_mask (0)
                               : (both_active and (((tmp_levels <= pi) and (best_levels > pi)) or
                                                   (tmp_levels < best_levels)));
    lane_mask bl_earlier = pindex_negative
                               ? lane_mask (0)
                               : (both_active and (((best_levels <= pi) and (tmp_levels > pi)) or
                                                   (best_levels < tmp_levels)));

    lane_mask both_past =
        pindex_negative ? both_active : (both_active and (tmp_levels > pi) and (best_levels > pi));
    lane_mask levels_eq = both_active and !tl_earlier and !bl_earlier and !both_past;

    lane_mask tmp_lead1 = (tmp_bits & lanes (1)) > lanes (0);
    lane_mask best_lead1 = (best_bits & lanes (1)) > lanes (0);

    lane_mask tmp_decides = tmp_extra or tl_earlier;
    lane_mask best_decides = best_extra or bl_earlier;

    lane_mask a_wins = (tmp_decides and tmp_lead1) or (best_decides and !best_lead1) or
                       (levels_eq and (a_greater or (!a_less and b_less)));

    lane_mask b_wins = (tmp_decides and !tmp_lead1) or (best_decides and best_lead1) or
                       (levels_eq and (b_greater or (a_less and !b_less)));

    int cutoff = find_first_set (both_past);
    lane_mask before_cutoff =
        (cutoff >= 8) ? lane_mask (0xffff) : LANE_INDICES < lanes (static_cast<uint16_t> (cutoff));
    lane_mask deciding = (a_wins or b_wins) and before_cutoff;
    int first = find_first_set (deciding);
    if (first >= 8)
      return 0;
    return a_wins[first] ? 1 : -1;
  }

}
