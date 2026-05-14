#ifndef ROFI_FZF_H
#define ROFI_FZF_H

#include <glib.h>
#include <limits.h>

/**
 * A C port of fzf's matching algorithms (src/algo/algo.go in junegunn/fzf).
 * The port mirrors fzf's "default" scoring scheme: bonusBoundaryWhite = 10,
 * bonusBoundaryDelimiter = 9, initialCharClass = white. The DP, bonus matrix,
 * and constants are 1:1 with FuzzyMatchV2.
 */

/** Returned when no match is found. Less than any valid score. */
#define FZF_NO_MATCH (INT_MIN / 2)

/**
 * Build the bonus matrix and ASCII char-class table. Idempotent.
 */
void fzf_init(void);

/**
 * FuzzyMatchV2: optimal-alignment fuzzy match.
 *
 * @param pattern         pattern as gunichar array (already lowercased when !case_sensitive)
 * @param plen            pattern length in chars
 * @param text            candidate as gunichar array
 * @param tlen            candidate length in chars
 * @param case_sensitive  if FALSE, pattern MUST be pre-lowercased; text is case-folded inline
 * @param out_pos         optional buffer of plen ints; receives matched-character indices
 *                        (in pattern order, from last pattern char back to first — caller
 *                        sorts if it cares about ascending order)
 * @param out_npos        optional; receives number of positions written
 * @param out_start       optional; receives char index of first matched pattern char
 * @param out_end         optional; receives char index one past last matched pattern char
 * @return score on match (>= 0), FZF_NO_MATCH otherwise.
 */
int fzf_fuzzy_match_v2(const gunichar *pattern, glong plen,
                       const gunichar *text, glong tlen,
                       gboolean case_sensitive,
                       int *out_pos, int *out_npos,
                       int *out_start, int *out_end);

/** Exact substring match ("'foo" in fzf). Highest-bonus alignment wins. */
int fzf_exact_match(const gunichar *pattern, glong plen,
                    const gunichar *text, glong tlen,
                    gboolean case_sensitive,
                    int *out_pos, int *out_npos,
                    int *out_start, int *out_end);

/** Prefix match ("^foo"). */
int fzf_prefix_match(const gunichar *pattern, glong plen,
                     const gunichar *text, glong tlen,
                     gboolean case_sensitive,
                     int *out_pos, int *out_npos,
                     int *out_start, int *out_end);

/** Suffix match ("foo$"). */
int fzf_suffix_match(const gunichar *pattern, glong plen,
                     const gunichar *text, glong tlen,
                     gboolean case_sensitive,
                     int *out_pos, int *out_npos,
                     int *out_start, int *out_end);

/** Equality match ("^foo$"). */
int fzf_equal_match(const gunichar *pattern, glong plen,
                    const gunichar *text, glong tlen,
                    gboolean case_sensitive,
                    int *out_pos, int *out_npos,
                    int *out_start, int *out_end);

/**
 * Convenience: decode a UTF-8 string to a freshly-allocated gunichar array.
 * On return, *out_len is the char count. Caller frees with g_free().
 * Returns NULL on encoding error.
 */
gunichar *fzf_utf8_to_runes(const char *s, glong byte_len, glong *out_len);

#endif /* ROFI_FZF_H */
