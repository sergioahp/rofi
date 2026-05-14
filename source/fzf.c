/*
 * Faithful C port of fzf's matching algorithms.
 * Mirrors junegunn/fzf src/algo/algo.go (default scheme).
 *
 * Differences from the Go original:
 *   - No slab arena; each call mallocs its own scratch (rofi allocates per
 *     candidate already, so the overhead is in the noise compared to UTF-8
 *     decoding and pango).
 *   - normalize=true is honored by the caller, not here. fzf carries a large
 *     Latin decomposition table; rofi already has g_utf8_normalize so we let
 *     the integration layer pre-normalize when configured.
 *   - We always run the V2 DP; we don't fall back to V1 for large M*N like
 *     fzf does (V1 needs separate code, and rofi already caps candidate
 *     lengths at 256 elsewhere). Patterns with M > 1000 return NO_MATCH.
 *
 * Tested against fzf's algo_test.go cases in test/fzf-test.c.
 */

#include <glib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "fzf.h"

/* ----- Scoring constants (match algo.go) ----- */
#define SCORE_MATCH                 16
#define SCORE_GAP_START             (-3)
#define SCORE_GAP_EXTENSION         (-1)
#define BONUS_BOUNDARY              (SCORE_MATCH / 2)                        /*  8 */
#define BONUS_NON_WORD              (SCORE_MATCH / 2)                        /*  8 */
#define BONUS_CAMEL_123             (BONUS_BOUNDARY + SCORE_GAP_EXTENSION)   /*  7 */
#define BONUS_CONSECUTIVE           (-(SCORE_GAP_START + SCORE_GAP_EXTENSION))/*  4 */
#define BONUS_FIRST_CHAR_MULTIPLIER 2

/* "default" scheme tunables (algo.go Init()) */
static int16_t bonus_boundary_white     = BONUS_BOUNDARY + 2;  /* 10 */
static int16_t bonus_boundary_delimiter = BONUS_BOUNDARY + 1;  /*  9 */

static const char *DELIMITER_CHARS = "/,:;|";
static const char *WHITE_CHARS     = " \t\n\v\f\r\x85\xA0";

typedef enum {
  CC_WHITE      = 0,
  CC_NON_WORD   = 1,
  CC_DELIMITER  = 2,
  CC_LOWER      = 3,
  CC_UPPER      = 4,
  CC_LETTER     = 5,
  CC_NUMBER     = 6,
  CC_COUNT      = 7,
} char_class;

static const char_class INITIAL_CLASS = CC_WHITE;

static char_class ascii_class[128];
static int16_t    bonus_matrix[CC_COUNT][CC_COUNT];
static gboolean   initialized = FALSE;

/* ----- Char classification ----- */

static char_class class_of_nonascii(gunichar c) {
  if (g_unichar_islower(c)) return CC_LOWER;
  if (g_unichar_isupper(c)) return CC_UPPER;
  /* fzf checks IsNumber before IsLetter — order matters for digits with letter
   * properties (rare). */
  if (g_unichar_isdigit(c)) return CC_NUMBER;
  if (g_unichar_isalpha(c)) return CC_LETTER;
  if (g_unichar_isspace(c)) return CC_WHITE;
  /* Delimiter set is ASCII-only in the default scheme. */
  return CC_NON_WORD;
}

static inline char_class class_of(gunichar c) {
  if (c < 128) return ascii_class[c];
  return class_of_nonascii(c);
}

static int16_t bonus_for(char_class prev, char_class cur) {
  /* algo.go bonusFor */
  if (cur > CC_NON_WORD) {
    switch (prev) {
      case CC_WHITE:     return bonus_boundary_white;
      case CC_DELIMITER: return bonus_boundary_delimiter;
      case CC_NON_WORD:  return BONUS_BOUNDARY;
      default: break;
    }
  }
  if ((prev == CC_LOWER && cur == CC_UPPER) ||
      (prev != CC_NUMBER && cur == CC_NUMBER)) {
    return BONUS_CAMEL_123;
  }
  switch (cur) {
    case CC_NON_WORD:
    case CC_DELIMITER:
      return BONUS_NON_WORD;
    case CC_WHITE:
      return bonus_boundary_white;
    default: break;
  }
  return 0;
}

static int16_t bonus_at(const gunichar *T, int idx) {
  if (idx == 0) return bonus_boundary_white;
  return bonus_matrix[class_of(T[idx - 1])][class_of(T[idx])];
}

void fzf_init(void) {
  if (initialized) return;
  for (int i = 0; i < 128; i++) {
    char c = (char)i;
    char_class cls = CC_NON_WORD;
    if (c >= 'a' && c <= 'z')      cls = CC_LOWER;
    else if (c >= 'A' && c <= 'Z') cls = CC_UPPER;
    else if (c >= '0' && c <= '9') cls = CC_NUMBER;
    else if (strchr(WHITE_CHARS, c))     cls = CC_WHITE;
    else if (strchr(DELIMITER_CHARS, c)) cls = CC_DELIMITER;
    ascii_class[i] = cls;
  }
  for (int i = 0; i < CC_COUNT; i++)
    for (int j = 0; j < CC_COUNT; j++)
      bonus_matrix[i][j] = bonus_for((char_class)i, (char_class)j);
  initialized = TRUE;
}

/* ----- Public: utf8 → runes helper ----- */

gunichar *fzf_utf8_to_runes(const char *s, glong byte_len, glong *out_len) {
  if (s == NULL) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  glong nchars = 0;
  gunichar *runes = g_utf8_to_ucs4_fast(s, byte_len, &nchars);
  if (out_len) *out_len = nchars;
  return runes;
}

/* ----- FuzzyMatchV2 ----- */

static inline int16_t max3_i16(int16_t a, int16_t b, int16_t c) {
  int16_t m = a > b ? a : b;
  return m > c ? m : c;
}

int fzf_fuzzy_match_v2(const gunichar *pattern, glong M_in,
                       const gunichar *text_in, glong N_in,
                       gboolean case_sensitive,
                       int *out_pos, int *out_npos,
                       int *out_start, int *out_end) {
  if (!initialized) fzf_init();
  if (out_npos)  *out_npos  = 0;
  if (out_start) *out_start = 0;
  if (out_end)   *out_end   = 0;

  if (M_in == 0) return 0;
  if (M_in > N_in) return FZF_NO_MATCH;
  /* algo.go caps M at 1000 to avoid int16 overflow in score matrix. */
  if (M_in > 1000) return FZF_NO_MATCH;

  const int M = (int)M_in;
  const int N = (int)N_in;

  /* Working buffers:
   *   T   – case-folded text (we need original-class B[], but compare folded)
   *   H0  – row 0 of H matrix (best score using pattern[0..0] ending at col c)
   *   C0  – consecutive-chunk length parallel to H0
   *   B   – bonus[col] = bonusMatrix[class(T[col-1])][class(T[col])]
   *   F   – F[pi] = first occurrence column of pattern[pi] after F[pi-1]
   */
  gunichar *T = g_malloc_n((size_t)N, sizeof(gunichar));
  int16_t  *H0 = g_malloc_n((size_t)N, sizeof(int16_t));
  int16_t  *C0 = g_malloc_n((size_t)N, sizeof(int16_t));
  int16_t  *B  = g_malloc_n((size_t)N, sizeof(int16_t));
  int32_t  *F  = g_malloc_n((size_t)M, sizeof(int32_t));

  /* Phase 2: bonus + H0/C0 + F[] + lastIdx. */
  int16_t max_score = 0;
  int max_score_pos = 0;
  int pidx = 0;
  int lastIdx = 0;
  const gunichar pchar0 = pattern[0];
  gunichar pchar = pattern[0];
  int16_t prev_h0 = 0;
  char_class prev_class = INITIAL_CLASS;
  gboolean in_gap = FALSE;
  gboolean early_exit = FALSE;

  for (int off = 0; off < N; off++) {
    gunichar c = text_in[off];
    char_class cls = class_of(c);
    if (!case_sensitive && cls == CC_UPPER) {
      c = g_unichar_tolower(c);
    }
    T[off] = c;

    int16_t bonus = bonus_matrix[prev_class][cls];
    B[off] = bonus;
    prev_class = cls;

    /* Track pattern walk for F[] and lastIdx */
    if (c == pchar) {
      if (pidx < M) {
        F[pidx] = off;
        pidx++;
        pchar = pattern[(pidx < M) ? pidx : M - 1];
      }
      lastIdx = off;
    }

    /* Compute H0[off] / C0[off] */
    if (c == pchar0) {
      int16_t score = (int16_t)(SCORE_MATCH + bonus * BONUS_FIRST_CHAR_MULTIPLIER);
      H0[off] = score;
      C0[off] = 1;
      if (M == 1 && score > max_score) {
        max_score = score;
        max_score_pos = off;
        if (bonus >= BONUS_BOUNDARY) {
          early_exit = TRUE;
          break;
        }
      }
      in_gap = FALSE;
    } else {
      int16_t s = (int16_t)(prev_h0 + (in_gap ? SCORE_GAP_EXTENSION : SCORE_GAP_START));
      H0[off] = s > 0 ? s : 0;
      C0[off] = 0;
      in_gap = TRUE;
    }
    prev_h0 = H0[off];
  }

  if (!early_exit && pidx != M) {
    g_free(T); g_free(H0); g_free(C0); g_free(B); g_free(F);
    return FZF_NO_MATCH;
  }

  if (M == 1) {
    if (out_start) *out_start = max_score_pos;
    if (out_end)   *out_end   = max_score_pos + 1;
    if (out_pos && out_npos) {
      out_pos[0] = max_score_pos;
      *out_npos = 1;
    }
    g_free(T); g_free(H0); g_free(C0); g_free(B); g_free(F);
    return (int)max_score;
  }

  /* Phase 3: fill score matrix H and consecutive matrix C, both width × M. */
  const int f0 = (int)F[0];
  const int width = lastIdx - f0 + 1;
  int16_t *H = g_malloc_n((size_t)width * (size_t)M, sizeof(int16_t));
  int16_t *C = g_malloc_n((size_t)width * (size_t)M, sizeof(int16_t));

  /* Row 0 = the slice of H0/C0 over [f0, lastIdx]. */
  for (int j = 0; j < width; j++) {
    H[j] = H0[f0 + j];
    C[j] = C0[f0 + j];
  }

  for (int pi = 1; pi < M; pi++) {
    const int f = (int)F[pi];
    const gunichar pc = pattern[pi];
    const int row = pi * width;
    const int rel_f = f - f0;
    /* Hleft[0] = 0 (boundary cell). algo.go: Hleft[0] = 0. */
    H[row + rel_f - 1] = 0;

    gboolean ig = FALSE;
    for (int col = f; col <= lastIdx; col++) {
      const int j = col - f0;
      const gunichar c = T[col];
      int16_t s1 = 0, s2;
      int16_t consecutive = 0;

      /* s2 from gap. (Hleft[off]) */
      int16_t hleft = H[row + j - 1];
      s2 = (int16_t)(hleft + (ig ? SCORE_GAP_EXTENSION : SCORE_GAP_START));

      if (pc == c) {
        int16_t hdiag = H[row - width + j - 1];
        s1 = (int16_t)(hdiag + SCORE_MATCH);
        int16_t b = B[col];
        consecutive = (int16_t)(C[row - width + j - 1] + 1);
        if (consecutive > 1) {
          int16_t fb = B[col - consecutive + 1];
          if (b >= BONUS_BOUNDARY && b > fb) {
            consecutive = 1;
          } else {
            b = max3_i16(b, BONUS_CONSECUTIVE, fb);
          }
        }
        if ((int16_t)(s1 + b) < s2) {
          s1 = (int16_t)(s1 + B[col]);
          consecutive = 0;
        } else {
          s1 = (int16_t)(s1 + b);
        }
      }
      C[row + j] = consecutive;

      ig = s1 < s2;
      int16_t score = s1 > s2 ? s1 : s2;
      if (score < 0) score = 0;
      if (pi == M - 1 && score > max_score) {
        max_score = score;
        max_score_pos = col;
      }
      H[row + j] = score;
    }
  }

  /* Phase 4: backtrace positions. Only run if caller asked. */
  int npos = 0;
  int start_col = f0;
  if (out_pos) {
    int i = M - 1;
    int j = max_score_pos;
    gboolean prefer_match = TRUE;
    for (;;) {
      const int I = i * width;
      const int j0 = j - f0;
      const int16_t s = H[I + j0];
      int16_t s1 = 0, s2 = 0;
      if (i > 0 && j >= (int)F[i]) {
        s1 = H[I - width + j0 - 1];
      }
      if (j > (int)F[i]) {
        s2 = H[I + j0 - 1];
      }
      if (s > s1 && (s > s2 || (s == s2 && prefer_match))) {
        out_pos[npos++] = j;
        if (i == 0) {
          start_col = j;
          break;
        }
        i--;
      }
      gboolean a_cons = C[I + j0] > 1;
      gboolean b_cons = FALSE;
      const int next = I + width + j0 + 1;
      if (next < width * M) {
        b_cons = C[next] > 0;
      }
      prefer_match = a_cons || b_cons;
      j--;
    }
  }
  if (out_npos)  *out_npos  = npos;
  if (out_start) *out_start = start_col;
  if (out_end)   *out_end   = max_score_pos + 1;

  g_free(T); g_free(H0); g_free(C0); g_free(B); g_free(F);
  g_free(H); g_free(C);
  return (int)max_score;
}

/* Forward decl: defined below. */
static int calculate_score_contiguous(const gunichar *t, int sidx, int eidx,
                                      const gunichar *p, gboolean case_sensitive);

/* ----- ExactMatchNaive (no boundary check) ----- */

static int exact_match_impl(const gunichar *pattern, glong M_in,
                            const gunichar *text_in, glong N_in,
                            gboolean case_sensitive,
                            gboolean boundary_check,
                            int *out_pos, int *out_npos,
                            int *out_start, int *out_end) {
  if (!initialized) fzf_init();
  if (out_npos)  *out_npos  = 0;
  if (out_start) *out_start = 0;
  if (out_end)   *out_end   = 0;

  const int M = (int)M_in;
  const int N = (int)N_in;
  if (M == 0) return 0;
  if (N < M) return FZF_NO_MATCH;

  int pidx = 0;
  int best_pos = -1;
  int16_t bonus = 0;
  int16_t bbonus = 0;
  int16_t best_bonus = -1;
  for (int index = 0; index < N; index++) {
    gunichar c = text_in[index];
    if (!case_sensitive && c >= 'A' && c <= 'Z') c += 32;
    else if (!case_sensitive && c > 127 && g_unichar_isupper(c)) c = g_unichar_tolower(c);

    gunichar pc = pattern[pidx];
    gboolean ok = pc == c;
    if (ok) {
      if (pidx == 0) bonus = bonus_at(text_in, index);
      if (boundary_check) {
        if (pidx == 0) {
          bbonus = bonus;
        }
        ok = bbonus >= BONUS_BOUNDARY;
        if (ok && pidx == 0) {
          ok = index == 0 || class_of(text_in[index - 1]) <= CC_DELIMITER;
        }
        if (ok && pidx == M - 1) {
          ok = index == N - 1 || class_of(text_in[index + 1]) <= CC_DELIMITER;
        }
      }
    }
    if (ok) {
      pidx++;
      if (pidx == M) {
        if (bonus > best_bonus) {
          best_pos = index;
          best_bonus = bonus;
        }
        if (bonus >= BONUS_BOUNDARY) break;
        index -= pidx - 1;
        pidx = 0;
        bonus = 0;
      }
    } else {
      index -= pidx;
      pidx = 0;
      bonus = 0;
    }
  }

  if (best_pos < 0) return FZF_NO_MATCH;
  const int sidx = best_pos - M + 1;
  const int eidx = best_pos + 1;
  int score = 0;
  if (boundary_check) {
    /* algo.go exactMatchNaive: boundary path uses an inflated score so
     * `'foo` can compete with `bar` in `'foo | bar`. */
    score = best_bonus;
    int deduct = (int)best_bonus - BONUS_BOUNDARY + 1;
    if (sidx > 0 && text_in[sidx - 1] == '_') {
      score -= deduct + 1;
      deduct = 1;
    }
    if (eidx < N && text_in[eidx] == '_') {
      score -= deduct;
    }
    score += SCORE_MATCH * M + (int)bonus_boundary_white * (M + 1);
  } else {
    score = calculate_score_contiguous(text_in, sidx, eidx, pattern, case_sensitive);
  }
  if (out_start) *out_start = sidx;
  if (out_end)   *out_end   = eidx;
  if (out_pos && out_npos) {
    for (int k = 0; k < M; k++) out_pos[k] = sidx + k;
    *out_npos = M;
  }
  return score;
}

int fzf_exact_match(const gunichar *p, glong plen, const gunichar *t, glong tlen,
                    gboolean cs, int *op, int *on, int *os, int *oe) {
  return exact_match_impl(p, plen, t, tlen, cs, FALSE, op, on, os, oe);
}

/* ----- PrefixMatch / SuffixMatch / EqualMatch ----- */

static int leading_whitespaces(const gunichar *t, int n) {
  int i = 0;
  while (i < n && g_unichar_isspace(t[i])) i++;
  return i;
}
static int trailing_whitespaces(const gunichar *t, int n) {
  int i = 0;
  while (i < n && g_unichar_isspace(t[n - 1 - i])) i++;
  return i;
}

static int calculate_score_contiguous(const gunichar *t, int sidx, int eidx,
                                      const gunichar *p, gboolean case_sensitive) {
  /* Mirrors algo.go calculateScore(). Walks t[sidx:eidx], expecting characters
   * to advance the pattern; mismatches accumulate gap penalty. */
  int score = 0;
  int consecutive = 0;
  int16_t first_bonus = 0;
  char_class prev_class = INITIAL_CLASS;
  if (sidx > 0) prev_class = class_of(t[sidx - 1]);
  int pidx = 0;
  gboolean in_gap = FALSE;
  for (int idx = sidx; idx < eidx; idx++) {
    gunichar c = t[idx];
    char_class cls = class_of(c);
    if (!case_sensitive && c >= 'A' && c <= 'Z') c += 32;
    else if (!case_sensitive && c > 127 && g_unichar_isupper(c)) c = g_unichar_tolower(c);
    if (c == p[pidx]) {
      score += SCORE_MATCH;
      int16_t bonus = bonus_matrix[prev_class][cls];
      if (consecutive == 0) {
        first_bonus = bonus;
      } else {
        if (bonus >= BONUS_BOUNDARY && bonus > first_bonus) first_bonus = bonus;
        bonus = max3_i16(bonus, first_bonus, BONUS_CONSECUTIVE);
      }
      if (pidx == 0) score += (int)bonus * BONUS_FIRST_CHAR_MULTIPLIER;
      else           score += (int)bonus;
      in_gap = FALSE;
      consecutive++;
      pidx++;
    } else {
      score += in_gap ? SCORE_GAP_EXTENSION : SCORE_GAP_START;
      in_gap = TRUE;
      consecutive = 0;
      first_bonus = 0;
    }
    prev_class = cls;
  }
  return score;
}

int fzf_prefix_match(const gunichar *p, glong plen, const gunichar *t, glong tlen,
                     gboolean case_sensitive,
                     int *out_pos, int *out_npos, int *out_start, int *out_end) {
  if (!initialized) fzf_init();
  if (out_npos)  *out_npos  = 0;
  if (out_start) *out_start = 0;
  if (out_end)   *out_end   = 0;
  if (plen == 0) return 0;

  int trim = 0;
  if (!g_unichar_isspace(p[0])) trim = leading_whitespaces(t, (int)tlen);
  if ((int)tlen - trim < (int)plen) return FZF_NO_MATCH;

  for (int i = 0; i < plen; i++) {
    gunichar c = t[trim + i];
    if (!case_sensitive) {
      if (c >= 'A' && c <= 'Z') c += 32;
      else if (c > 127 && g_unichar_isupper(c)) c = g_unichar_tolower(c);
    }
    if (c != p[i]) return FZF_NO_MATCH;
  }
  int score = calculate_score_contiguous(t, trim, trim + (int)plen, p, case_sensitive);
  if (out_start) *out_start = trim;
  if (out_end)   *out_end   = trim + (int)plen;
  if (out_pos && out_npos) {
    for (int k = 0; k < plen; k++) out_pos[k] = trim + k;
    *out_npos = (int)plen;
  }
  return score;
}

int fzf_suffix_match(const gunichar *p, glong plen, const gunichar *t, glong tlen,
                     gboolean case_sensitive,
                     int *out_pos, int *out_npos, int *out_start, int *out_end) {
  if (!initialized) fzf_init();
  if (out_npos)  *out_npos  = 0;
  if (out_start) *out_start = 0;
  if (out_end)   *out_end   = 0;

  int trim_end = (int)tlen;
  if (plen == 0 || !g_unichar_isspace(p[plen - 1])) {
    trim_end -= trailing_whitespaces(t, (int)tlen);
  }
  if (plen == 0) {
    if (out_start) *out_start = trim_end;
    if (out_end)   *out_end   = trim_end;
    return 0;
  }
  int diff = trim_end - (int)plen;
  if (diff < 0) return FZF_NO_MATCH;

  for (int i = 0; i < plen; i++) {
    gunichar c = t[diff + i];
    if (!case_sensitive) {
      if (c >= 'A' && c <= 'Z') c += 32;
      else if (c > 127 && g_unichar_isupper(c)) c = g_unichar_tolower(c);
    }
    if (c != p[i]) return FZF_NO_MATCH;
  }
  int sidx = diff;
  int eidx = trim_end;
  int score = calculate_score_contiguous(t, sidx, eidx, p, case_sensitive);
  if (out_start) *out_start = sidx;
  if (out_end)   *out_end   = eidx;
  if (out_pos && out_npos) {
    for (int k = 0; k < plen; k++) out_pos[k] = sidx + k;
    *out_npos = (int)plen;
  }
  return score;
}

int fzf_equal_match(const gunichar *p, glong plen, const gunichar *t, glong tlen,
                    gboolean case_sensitive,
                    int *out_pos, int *out_npos, int *out_start, int *out_end) {
  if (!initialized) fzf_init();
  if (out_npos)  *out_npos  = 0;
  if (out_start) *out_start = 0;
  if (out_end)   *out_end   = 0;
  if (plen == 0) return FZF_NO_MATCH;

  int trim_start = 0;
  if (!g_unichar_isspace(p[0])) trim_start = leading_whitespaces(t, (int)tlen);
  int trim_end = 0;
  if (!g_unichar_isspace(p[plen - 1])) trim_end = trailing_whitespaces(t, (int)tlen);

  if ((int)tlen - trim_start - trim_end != (int)plen) return FZF_NO_MATCH;
  for (int i = 0; i < plen; i++) {
    gunichar c = t[trim_start + i];
    if (!case_sensitive) {
      if (c >= 'A' && c <= 'Z') c += 32;
      else if (c > 127 && g_unichar_isupper(c)) c = g_unichar_tolower(c);
    }
    if (c != p[i]) return FZF_NO_MATCH;
  }
  int score = (SCORE_MATCH + (int)bonus_boundary_white) * (int)plen +
              (BONUS_FIRST_CHAR_MULTIPLIER - 1) * (int)bonus_boundary_white;
  if (out_start) *out_start = trim_start;
  if (out_end)   *out_end   = trim_start + (int)plen;
  if (out_pos && out_npos) {
    for (int k = 0; k < plen; k++) out_pos[k] = trim_start + k;
    *out_npos = (int)plen;
  }
  return score;
}
