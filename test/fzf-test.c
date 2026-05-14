/* Cross-check: scores and positions must match fzf's algo_test.go. */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fzf.h"

/* Mirror fzf's constants so the expected-value expressions below read the
 * same as the Go tests. */
#define SCORE_MATCH                 16
#define SCORE_GAP_START             (-3)
#define SCORE_GAP_EXTENSION         (-1)
#define BONUS_BOUNDARY              (SCORE_MATCH / 2)
#define BONUS_NON_WORD              (SCORE_MATCH / 2)
#define BONUS_CAMEL_123             (BONUS_BOUNDARY + SCORE_GAP_EXTENSION)
#define BONUS_CONSECUTIVE           (-(SCORE_GAP_START + SCORE_GAP_EXTENSION))
#define BONUS_FIRST_CHAR_MULTIPLIER 2
#define BONUS_BOUNDARY_WHITE        (BONUS_BOUNDARY + 2)
#define BONUS_BOUNDARY_DELIMITER    (BONUS_BOUNDARY + 1)

static int total = 0, failed = 0;

static int cmp_int(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}

typedef int (*algo_fn)(const gunichar *, glong, const gunichar *, glong,
                       gboolean, int *, int *, int *, int *);

static void run(const char *name, algo_fn fn, gboolean case_sensitive,
                const char *input, const char *pattern,
                int want_sidx, int want_eidx, int want_score) {
  total++;
  const char *pat_use = pattern;
  char *pat_lower = NULL;
  if (!case_sensitive) {
    pat_lower = g_utf8_strdown(pattern, -1);
    pat_use = pat_lower;
  }
  glong plen = 0, tlen = 0;
  gunichar *p = fzf_utf8_to_runes(pat_use, -1, &plen);
  gunichar *t = fzf_utf8_to_runes(input, -1, &tlen);

  int pos[64] = {0};
  int npos = 0, sidx = 0, eidx = 0;
  int score = fn(p, plen, t, tlen, case_sensitive, pos, &npos, &sidx, &eidx);

  int got_sidx, got_eidx;
  if (npos == 0) {
    got_sidx = sidx;
    got_eidx = eidx;
  } else {
    qsort(pos, npos, sizeof(int), cmp_int);
    got_sidx = pos[0];
    got_eidx = pos[npos - 1] + 1;
  }
  int got_score = (score == FZF_NO_MATCH) ? 0 : score;
  int expect_sidx = (want_score == 0 && want_sidx == -1) ? -1 : want_sidx;
  int expect_eidx = (want_score == 0 && want_eidx == -1) ? -1 : want_eidx;
  /* On no-match, our API leaves sidx=eidx=0 and returns FZF_NO_MATCH. The Go
   * test uses -1/-1; reconcile by promoting any miss to -1/-1. */
  if (score == FZF_NO_MATCH) {
    got_sidx = -1;
    got_eidx = -1;
  }
  gboolean ok = got_score == want_score && got_sidx == expect_sidx && got_eidx == expect_eidx;
  if (!ok) {
    failed++;
    fprintf(stderr,
            "FAIL [%s cs=%d] in=%s pat=%s : score=%d (want %d) [%d,%d) (want [%d,%d))\n",
            name, case_sensitive, input, pattern,
            got_score, want_score, got_sidx, got_eidx, expect_sidx, expect_eidx);
  }
  g_free(p);
  g_free(t);
  g_free(pat_lower);
}

static void test_fuzzy(void) {
  algo_fn fn = fzf_fuzzy_match_v2;
  /* Cases pulled from fzf src/algo/algo_test.go TestFuzzyMatch (forward only). */
  run("fuzzy", fn, FALSE, "fooBarbaz1", "oBZ", 2, 9,
      SCORE_MATCH*3 + BONUS_CAMEL_123 + SCORE_GAP_START + SCORE_GAP_EXTENSION*3);
  run("fuzzy", fn, FALSE, "foo bar baz", "fbb", 0, 9,
      SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER +
      BONUS_BOUNDARY_WHITE*2 + 2*SCORE_GAP_START + 4*SCORE_GAP_EXTENSION);
  run("fuzzy", fn, FALSE, "/AutomatorDocument.icns", "rdoc", 9, 13,
      SCORE_MATCH*4 + BONUS_CAMEL_123 + BONUS_CONSECUTIVE*2);
  run("fuzzy", fn, FALSE, "/man1/zshcompctl.1", "zshc", 6, 10,
      SCORE_MATCH*4 + BONUS_BOUNDARY_DELIMITER*BONUS_FIRST_CHAR_MULTIPLIER +
      BONUS_BOUNDARY_DELIMITER*3);
  run("fuzzy", fn, FALSE, "/.oh-my-zsh/cache", "zshc", 8, 13,
      SCORE_MATCH*4 + BONUS_BOUNDARY*BONUS_FIRST_CHAR_MULTIPLIER +
      BONUS_BOUNDARY*2 + SCORE_GAP_START + BONUS_BOUNDARY_DELIMITER);
  run("fuzzy", fn, FALSE, "ab0123 456", "12356", 3, 10,
      SCORE_MATCH*5 + BONUS_CONSECUTIVE*3 + SCORE_GAP_START + SCORE_GAP_EXTENSION);
  run("fuzzy", fn, FALSE, "abc123 456", "12356", 3, 10,
      SCORE_MATCH*5 + BONUS_CAMEL_123*BONUS_FIRST_CHAR_MULTIPLIER +
      BONUS_CAMEL_123*2 + BONUS_CONSECUTIVE + SCORE_GAP_START + SCORE_GAP_EXTENSION);
  run("fuzzy", fn, FALSE, "foo/bar/baz", "fbb", 0, 9,
      SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER +
      BONUS_BOUNDARY_DELIMITER*2 + 2*SCORE_GAP_START + 4*SCORE_GAP_EXTENSION);
  run("fuzzy", fn, FALSE, "fooBarBaz", "fbb", 0, 7,
      SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER +
      BONUS_CAMEL_123*2 + 2*SCORE_GAP_START + 2*SCORE_GAP_EXTENSION);
  run("fuzzy", fn, FALSE, "foo barbaz", "fbb", 0, 8,
      SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER + BONUS_BOUNDARY_WHITE +
      SCORE_GAP_START*2 + SCORE_GAP_EXTENSION*3);
  run("fuzzy", fn, FALSE, "fooBar Baz", "foob", 0, 4,
      SCORE_MATCH*4 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER + BONUS_BOUNDARY_WHITE*3);
  run("fuzzy", fn, FALSE, "xFoo-Bar Baz", "foo-b", 1, 6,
      SCORE_MATCH*5 + BONUS_CAMEL_123*BONUS_FIRST_CHAR_MULTIPLIER + BONUS_CAMEL_123*2 +
      BONUS_NON_WORD + BONUS_BOUNDARY);

  /* Case-sensitive */
  run("fuzzy", fn, TRUE, "fooBarbaz", "oBz", 2, 9,
      SCORE_MATCH*3 + BONUS_CAMEL_123 + SCORE_GAP_START + SCORE_GAP_EXTENSION*3);
  run("fuzzy", fn, TRUE, "Foo/Bar/Baz", "FBB", 0, 9,
      SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER + BONUS_BOUNDARY_DELIMITER*2 +
      SCORE_GAP_START*2 + SCORE_GAP_EXTENSION*4);
  run("fuzzy", fn, TRUE, "FooBarBaz", "FBB", 0, 7,
      SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER + BONUS_CAMEL_123*2 +
      SCORE_GAP_START*2 + SCORE_GAP_EXTENSION*2);
  run("fuzzy", fn, TRUE, "FooBar Baz", "FooB", 0, 4,
      SCORE_MATCH*4 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER + BONUS_BOUNDARY_WHITE*2 +
      MAX(BONUS_CAMEL_123, BONUS_BOUNDARY_WHITE));

  run("fuzzy", fn, TRUE, "foo-bar", "o-ba", 2, 6, SCORE_MATCH*4 + BONUS_BOUNDARY*3);

  /* Non-match */
  run("fuzzy", fn, TRUE, "fooBarbaz", "oBZ",       -1, -1, 0);
  run("fuzzy", fn, TRUE, "Foo Bar Baz", "fbb",     -1, -1, 0);
  run("fuzzy", fn, TRUE, "fooBarbaz", "fooBarbazz", -1, -1, 0);
}

static void test_exact(void) {
  /* algo_test.go TestExactMatchNaive (forward) */
  run("exact", fzf_exact_match, TRUE, "fooBarbaz", "oBA", -1, -1, 0);
  run("exact", fzf_exact_match, TRUE, "fooBarbaz", "fooBarbazz", -1, -1, 0);
  run("exact", fzf_exact_match, FALSE, "fooBarbaz", "oBA", 2, 5,
      SCORE_MATCH*3 + BONUS_CAMEL_123 + BONUS_CONSECUTIVE);
  run("exact", fzf_exact_match, FALSE, "/AutomatorDocument.icns", "rdoc", 9, 13,
      SCORE_MATCH*4 + BONUS_CAMEL_123 + BONUS_CONSECUTIVE*2);
  run("exact", fzf_exact_match, FALSE, "/man1/zshcompctl.1", "zshc", 6, 10,
      SCORE_MATCH*4 + BONUS_BOUNDARY_DELIMITER*(BONUS_FIRST_CHAR_MULTIPLIER+3));
  run("exact", fzf_exact_match, FALSE, "/.oh-my-zsh/cache", "zsh/c", 8, 13,
      SCORE_MATCH*5 + BONUS_BOUNDARY*(BONUS_FIRST_CHAR_MULTIPLIER+3) + BONUS_BOUNDARY_DELIMITER);
}

static void test_prefix(void) {
  const int score = SCORE_MATCH*3 + BONUS_BOUNDARY_WHITE*BONUS_FIRST_CHAR_MULTIPLIER +
                    BONUS_BOUNDARY_WHITE*2;
  run("prefix", fzf_prefix_match, TRUE,  "fooBarbaz", "Foo", -1, -1, 0);
  run("prefix", fzf_prefix_match, FALSE, "fooBarBaz", "baz", -1, -1, 0);
  run("prefix", fzf_prefix_match, FALSE, "fooBarbaz", "Foo", 0, 3, score);
  run("prefix", fzf_prefix_match, FALSE, "foOBarBaZ", "foo", 0, 3, score);
  run("prefix", fzf_prefix_match, FALSE, "f-oBarbaz", "f-o", 0, 3, score);
  run("prefix", fzf_prefix_match, FALSE, " fooBar",   "foo", 1, 4, score);
  run("prefix", fzf_prefix_match, FALSE, " fooBar",   " fo", 0, 3, score);
  run("prefix", fzf_prefix_match, FALSE, "     fo",   "foo", -1, -1, 0);
}

static void test_suffix(void) {
  run("suffix", fzf_suffix_match, TRUE,  "fooBarbaz", "Baz", -1, -1, 0);
  run("suffix", fzf_suffix_match, FALSE, "fooBarbaz", "Foo", -1, -1, 0);
  run("suffix", fzf_suffix_match, FALSE, "fooBarbaz", "baz", 6, 9,
      SCORE_MATCH*3 + BONUS_CONSECUTIVE*2);
  run("suffix", fzf_suffix_match, FALSE, "fooBarBaZ", "baz", 6, 9,
      (SCORE_MATCH + BONUS_CAMEL_123)*3 + BONUS_CAMEL_123*(BONUS_FIRST_CHAR_MULTIPLIER - 1));
  run("suffix", fzf_suffix_match, FALSE, "fooBarbaz ", "baz", 6, 9,
      SCORE_MATCH*3 + BONUS_CONSECUTIVE*2);
  run("suffix", fzf_suffix_match, FALSE, "fooBarbaz ", "baz ", 6, 10,
      SCORE_MATCH*4 + BONUS_CONSECUTIVE*2 + BONUS_BOUNDARY_WHITE);
}

/* Equivalent of rofi's utf8_helper_simplify_string — NFKD decompose + strip
 * combining marks. Lets us test the "normalize" workflow without pulling in
 * rofi's helper.c. */
static char *simplify_utf8(const char *s) {
  char *norm = g_utf8_normalize(s, -1, G_NORMALIZE_ALL);
  GString *out = g_string_new("");
  for (const char *p = norm; p && *p; p = g_utf8_next_char(p)) {
    gunichar c = g_utf8_get_char(p);
    if (!g_unichar_ismark(c)) {
      g_string_append_unichar(out, c);
    }
  }
  g_free(norm);
  return g_string_free(out, FALSE);
}

/* Variant of run() that simplifies both the input and pattern before passing
 * to the algorithm — this mirrors what the MM_FZF wiring does under
 * config.normalize_match. */
static void run_normalized(const char *name, algo_fn fn, const char *input,
                           const char *pattern, int want_sidx, int want_eidx,
                           int want_score) {
  char *in = simplify_utf8(input);
  char *pat = simplify_utf8(pattern);
  run(name, fn, FALSE, in, pat, want_sidx, want_eidx, want_score);
  g_free(in);
  g_free(pat);
}

static void test_normalize(void) {
  /* From fzf algo_test.go TestNormalize. */
  run_normalized("normalize", fzf_fuzzy_match_v2, "Só Danço Samba", "So", 0, 2, 62);
  run_normalized("normalize", fzf_fuzzy_match_v2, "Só Danço Samba", "sodc", 0, 7, 97);
  run_normalized("normalize", fzf_fuzzy_match_v2, "Danço", "danco", 0, 5, 140);
  run_normalized("normalize", fzf_prefix_match,   "Só Danço Samba", "So", 0, 2, 62);
  run_normalized("normalize", fzf_prefix_match,   "Danço", "danco", 0, 5, 140);
  run_normalized("normalize", fzf_suffix_match,   "Danço", "danco", 0, 5, 140);
  run_normalized("normalize", fzf_exact_match,    "Só Danço Samba", "So", 0, 2, 62);
  run_normalized("normalize", fzf_exact_match,    "Danço", "danco", 0, 5, 140);
  run_normalized("normalize", fzf_equal_match,    "Danço", "danco", 0, 5, 140);
}

static void test_empty(void) {
  run("fuzzy-empty",  fzf_fuzzy_match_v2, TRUE, "foobar", "", 0, 0, 0);
  run("exact-empty",  fzf_exact_match,    TRUE, "foobar", "", 0, 0, 0);
  run("prefix-empty", fzf_prefix_match,   TRUE, "foobar", "", 0, 0, 0);
  run("suffix-empty", fzf_suffix_match,   TRUE, "foobar", "", 6, 6, 0);
}

int main(void) {
  fzf_init();
  test_fuzzy();
  test_exact();
  test_prefix();
  test_suffix();
  test_normalize();
  test_empty();
  fprintf(stdout, "fzf-test: %d/%d passed, %d failed\n", total - failed, total, failed);
  return failed == 0 ? 0 : 1;
}
