/**
 * @file ui_dictionary.c
 * @brief The prediction sources this library ships: a word list, a trie, an emoji tag
 *        table, and a composite that chains any two of them.
 *
 * WHY THEY LIVE TOGETHER. All four answer the same two questions through
 * IrcitUiDictionaryInterface, and three of them are the same code with a different lookup:
 * the composite is defined by the interface, the emoji table is a two-column list, and the
 * trie is the general form of the list. Keeping them in one file is what makes "the widget
 * cannot tell which it is holding" reviewable rather than a claim -- there is one place where
 * a source could reach outside the interface, and nothing here does.
 *
 * THE TRIE IS THE ONLY ONE THAT IS NOT TRIVIAL, and its shape is a deliberate departure from
 * IRCITUILIB_AUTOCOMPLETE_DESIGN.md's section 4. That section declares
 * `IrcitUiTrieNode *children[TRIE_ALPHABET_SIZE]` with TRIE_ALPHABET_SIZE 256, which on LP64
 * is 2 KB of pointers per node before any payload; section 5 then ingests a file the same
 * document describes as 100,000 to 500,000 words. Prefix sharing bounds the node count below
 * the total character count but not usefully -- the count lands in the 10^5 to 10^6 range, so
 * the dense shape costs hundreds of megabytes to gigabytes for a few megabytes of text.
 * Children here are a sorted dynamic array of (byte, child) pairs instead, which is smaller
 * for the branching this data actually has and makes the case-folded traversal a scan rather
 * than an index.
 *
 * CASE FOLDING IS ASCII-ONLY, and that is a property of the design rather than of the code:
 * a UTF-8 byte string's continuation bytes are never in A-Z, so folding the lead and
 * interior bytes of a multi-byte sequence is a no-op on them. Folding beyond ASCII would need
 * a locale table this library does not carry, and would make the emoji table's tags match
 * case-insensitively in ways nobody asked for.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ircituilib/config_defs.h"
#include "ircituilib/ui.h"
#include "ui_log.h"

/* ---- the word list --------------------------------------------------------- */

/* A static word list. THE WORDS ARE BORROWED, NOT COPIED: the caller's array is the
   dictionary, and it must outlive the dictionary, which is the same contract
   IrcitUiDictionary::instance has with everything else. Copying would make the factory a
   silent allocation per word for a list that is almost always a file-scope array. */
typedef struct
{
  const char *const *words_ptr;
  size_t             count;
} sDictListInstance;

/* The nth list entry that completes `prefix_ptr`, counting from 0.

   Case-folded, because a layout's word list is written in lower case and a user types
   whatever their keyboard produces -- an exact-match lookup would make the feature appear to
   work only for people who type in caps. */
static int
sDictListNth(void *instance_ptr, const char *prefix_ptr, int ordinal, char *out_ptr, size_t out_sz)
{
  const sDictListInstance *list = instance_ptr;
  size_t                   plen, i;
  int                      seen = 0;

  if (!list || !list->words_ptr || !prefix_ptr || !out_ptr || out_sz == 0) return 0;
  plen = strlen(prefix_ptr);
  if (plen == 0) return 0;

  for (i = 0; i < list->count; i++) {
    const char *w = list->words_ptr[i];
    if (!w || strncasecmp(w, prefix_ptr, plen) != 0) continue;
    // A candidate equal to what was typed is not a completion: there is nothing left to offer, and accepting it would write the same bytes back.
    if (strlen(w) <= plen) continue;
    if (seen++ == ordinal) {
      size_t n = strlen(w);
      if (n + 1 > out_sz) n = out_sz - 1;
      memcpy(out_ptr, w, n);
      out_ptr[n] = 0;
      return 1;
    }
  }
  return 0;
}

static int
sDictListBest(void *instance_ptr, const char *prefix_ptr, char *out_ptr, size_t out_sz)
{
  return sDictListNth(instance_ptr, prefix_ptr, 0, out_ptr, out_sz);
}

static void
sDictListDestroy(void *instance_ptr)
{
  // The words are the caller's, so only this instance goes.
  free(instance_ptr);
}

PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateList(const char *const *words_ptr, size_t count)
{
  IrcitUiDictionary *dict;
  sDictListInstance *inst;

  if (!words_ptr || count == 0) return NULL;
  inst = calloc(1, sizeof *inst);
  if (!inst) return NULL;
  inst->words_ptr = words_ptr;
  inst->count     = count;

  dict = calloc(1, sizeof *dict);
  if (!dict) {
    free(inst);
    return NULL;
  }
  dict->instance    = inst;
  dict->api.best    = sDictListBest;
  dict->api.next    = sDictListNth;
  dict->api.destroy = sDictListDestroy;
  return dict;
}

/* ---- the trie -------------------------------------------------------------- */

/* One edge: the byte that selects it, and the node it reaches. */
typedef struct
{
  unsigned char byte;
  void         *child;
} sTrieEdge;

typedef struct
{
  sTrieEdge *edges;   ///< Sorted by `byte`, so a lookup is a binary search and the
  int        n_edges; ///< traversal order below is deterministic.
  int        cap;
  char      *word;    ///< Non-NULL at a terminal: the word as it was inserted.
  char      *payload; ///< What an accept writes instead, or NULL to write the tail.
  unsigned   weight;  ///< How often this word has been learned. See IrcitUiDictionaryLearn.
  int        is_static; ///< Immune to the prune sweep.
} sTrieNode;

typedef struct
{
  sTrieNode *root;
  size_t     nodes;
  int        fuzzy;   ///< Edit-distance threshold, or 0 for exact matching only.
} sTrieInstance;

/* The ASCII fold, applied to one byte. Everything at or above 0x80 passes through, which is
   what keeps multi-byte sequences intact: no continuation byte is in A-Z. */
static unsigned char
sTrieFold(unsigned char b)
{
  return (b >= 'A' && b <= 'Z') ? (unsigned char)(b + 32) : b;
}

static sTrieNode *
sTrieNodeNew(void)
{
  return calloc(1, sizeof(sTrieNode));
}

static void
sTrieNodeFree(sTrieNode *n)
{
  int i;
  if (!n) return;
  for (i = 0; i < n->n_edges; i++) sTrieNodeFree(n->edges[i].child);
  free(n->edges);
  free(n->word);
  free(n->payload);
  free(n);
}

/* The child reached by `b`, or NULL. Binary search: `edges` is kept sorted. */
/* Declared ahead of the exact query, which falls back to it: the approximate tier is defined
   below with the rest of its machinery, and one forward declaration is cheaper than moving a
   hundred lines of it above its only caller. */
static sTrieNode *sTrieFuzzy(sTrieInstance *trie, const char *prefix_ptr, unsigned *dist_ptr);

static sTrieNode *
sTrieChild(const sTrieNode *n, unsigned char b)
{
  int lo = 0, hi = n->n_edges - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (n->edges[mid].byte == b) return n->edges[mid].child;
    if (n->edges[mid].byte < b) lo = mid + 1;
    else hi = mid - 1;
  }
  return NULL;
}

/* The child reached by `b`, created if it is not there. NULL only on allocation failure. */
static sTrieNode *
sTrieChildEnsure(sTrieNode *n, unsigned char b)
{
  int lo = 0, hi = n->n_edges - 1, at;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (n->edges[mid].byte == b) return n->edges[mid].child;
    if (n->edges[mid].byte < b) lo = mid + 1;
    else hi = mid - 1;
  }
  // `lo` is the insertion point, and the search left the array sorted.
  at = lo;
  if (n->n_edges == n->cap) {
    int        ncap = n->cap ? n->cap * 2 : 4;
    sTrieEdge *grown = realloc(n->edges, (size_t)ncap * sizeof *grown);
    if (!grown) return NULL;
    n->edges = grown;
    n->cap   = ncap;
  }
  memmove(&n->edges[at + 1], &n->edges[at], (size_t)(n->n_edges - at) * sizeof *n->edges);
  n->edges[at].byte  = b;
  n->edges[at].child = sTrieNodeNew();
  if (!n->edges[at].child) return NULL;
  n->n_edges++;
  return n->edges[at].child;
}

/* Walk to the node for `word_ptr`, creating the path, and keep the node count honest.
 *
 * ONE WALK FOR BOTH CALLERS, and the reason is a defect rather than tidiness: the first version
 * had the walk written out in Insert and again in Learn, and only Insert counted. So learning a
 * word built its path and left `nodes` at 1 -- and the demo's readout, which prints that count,
 * said "1 nodes" whether or not anything had been learned. An operation that works and reports
 * that it did nothing is worse than one that fails. */
static sTrieNode *
sTrieDescendEnsure(sTrieInstance *trie, const char *word_ptr)
{
  const unsigned char *p;
  sTrieNode           *n;

  if (!trie || !trie->root || !word_ptr || !word_ptr[0]) return NULL;
  n = trie->root;
  for (p = (const unsigned char *)word_ptr; *p; p++) {
    int        existed = sTrieChild(n, sTrieFold(*p)) != NULL;
    sTrieNode *next    = sTrieChildEnsure(n, sTrieFold(*p));
    if (!next) return NULL;
    if (!existed) trie->nodes++;
    n = next;
  }
  return n;
}

/* Give a node its word if it has none. The FIRST casing inserted is the one kept, which is
   what makes re-inserting "Banana" over "banana" a no-op rather than a rewrite. */
static int
sTrieSetWord(sTrieNode *n, const char *word_ptr)
{
  char *copy;
  if (!n || n->word) return 1;
  copy = malloc(strlen(word_ptr) + 1);
  if (!copy) return 0;
  strcpy(copy, word_ptr);
  n->word = copy;
  return 1;
}

static sTrieNode *
sTrieDescend(const sTrieInstance *trie, const char *prefix_ptr)
{
  const unsigned char *p = (const unsigned char *)prefix_ptr;
  sTrieNode           *n;
  if (!trie || !trie->root || !prefix_ptr) return NULL;
  n = trie->root;
  while (*p) {
    n = sTrieChild(n, sTrieFold(*p++));
    if (!n) return NULL;
  }
  return n;
}

/* The heaviest terminal in this subtree, ties broken by the traversal order below and by
   nothing else.

   THE TIE-BREAK IS SPECIFIED RATHER THAN LEFT TO THE TRAVERSAL, because an unspecified one is
   a flaky probe and a demo that reorders itself between runs. Equal weights go to the
   lexicographically smaller word -- which the edge-sorted DFS below yields first, so `>`
   rather than `>=` is the whole of the rule. */
static void
sTrieBest(sTrieNode *n, sTrieNode **best_ptr)
{
  int i;
  if (!n) return;
  if (n->word) {
    if (!*best_ptr || n->weight > (*best_ptr)->weight) *best_ptr = n;
  }
  for (i = 0; i < n->n_edges; i++) sTrieBest(n->edges[i].child, best_ptr);
}

/* The nth terminal in the same order sTrieBest walks, so ordinal 0 is what `best` returns
   and a list built from `next` agrees with the inline hint. */
static void
sTrieNth(sTrieNode *n, int *seen, int ordinal, sTrieNode **found_ptr)
{
  int i;
  if (!n || *found_ptr) return;
  if (n->word) {
    if ((*seen)++ == ordinal) {
      *found_ptr = n;
      return;
    }
  }
  for (i = 0; i < n->n_edges && !*found_ptr; i++) sTrieNth(n->edges[i].child, seen, ordinal, found_ptr);
}

/* Copy a node's word into the caller's buffer, truncated to fit. */
static int
sTrieEmit(const sTrieNode *n, char *out_ptr, size_t out_sz)
{
  size_t len;
  if (!n || !n->word || !out_ptr || out_sz == 0) return 0;
  len = strlen(n->word);
  if (len + 1 > out_sz) len = out_sz - 1;
  memcpy(out_ptr, n->word, len);
  out_ptr[len] = 0;
  return 1;
}

/* What an accept should write for this candidate: the payload, or nothing.

   THE REPLACEMENT IS A PURE FUNCTION OF THE CANDIDATE, which is why it is its own entry
   rather than an extra out-parameter on `best` and `next`. The alternative -- each query
   handing back the replacement alongside the candidate -- meant every source had to carry a
   value only one of them has, and meant the widget had to hold it from one call to the next.
   Asked this way, a source that has no payloads returns 0 and the widget writes the
   candidate's own tail, which is the whole of the default behaviour. */
static int
sTrieReplace(void *instance_ptr, const char *candidate_ptr, char *out_ptr, size_t out_sz)
{
  sTrieNode *n = sTrieDescend(instance_ptr, candidate_ptr);
  size_t     len;
  if (!n || !n->payload || !out_ptr || out_sz == 0) return 0;
  len = strlen(n->payload);
  if (len + 1 > out_sz) len = out_sz - 1;
  memcpy(out_ptr, n->payload, len);
  out_ptr[len] = 0;
  return 1;
}

static int
sTrieQueryBest(void *instance_ptr, const char *prefix_ptr, char *out_ptr, size_t out_sz)
{
  sTrieInstance *trie = instance_ptr;
  sTrieNode     *sub, *best = NULL;

  sub = sTrieDescend(trie, prefix_ptr);
  if (sub) {
    sTrieBest(sub, &best);
    if (best) return sTrieEmit(best, out_ptr, out_sz);
  }
  // THE SECOND TIER. Reached only when the exact walk found nothing -- either no such path, or a path with no terminal under it -- which is exactly the typo case section 13 is for.
  best = sTrieFuzzy(trie, prefix_ptr, NULL);
  return sTrieEmit(best, out_ptr, out_sz);
}

static int
sTrieQueryNth(void *instance_ptr, const char *prefix_ptr, int ordinal, char *out_ptr, size_t out_sz)
{
  sTrieNode *sub, *found = NULL;
  int        seen = 0;
  if (ordinal < 0) return 0;
  sub = sTrieDescend(instance_ptr, prefix_ptr);
  if (!sub) return 0;
  sTrieNth(sub, &seen, ordinal, &found);
  return sTrieEmit(found, out_ptr, out_sz);
}


/* ---- the approximate tier --------------------------------------------------- */

/* Branch-and-bound Levenshtein over the trie, from the design document's section 13.
 *
 * WHY IT IS A SECOND TIER AND NOT A FIRST: an exact prefix walk costs the length of what was
 * typed and finds the answer in the common case, because the common case is a word the user is
 * spelling correctly. Approximate matching costs a walk of the SUBTREE, so it is asked only
 * when the exact walk came back with nothing -- which is what makes "Aple" find "apple"
 * without every keystroke paying for the possibility that it might not.
 *
 * ONE ROW ARRAY PER DEPTH, carried down the recursion, so the distance is incremental rather
 * than recomputed per candidate -- a full matrix per word is the thing that makes fuzzy search
 * too slow to sit under a keystroke.
 *
 * THE ROW IS HEAP-ALLOCATED ONCE PER QUERY and shared by the recursion rather than being
 * alloca'd per call: the design document's version allocates a row at every level, which on a
 * deep path is a stack overflow waiting for a long word. Depth is bounded by the row length,
 * which is bounded by the prefix. */
typedef struct
{
  const char *target;
  size_t      tlen;
  unsigned    threshold;
  sTrieNode  *best;
  unsigned    best_dist;
  size_t      depth;
  unsigned   *rows;  ///< One row per depth, (tlen+1) entries each.
} sFuzzyCtx;

/* The three-way minimum the recurrence needs. */
static unsigned
sFuzzyMin3(unsigned a, unsigned b, unsigned c)
{
  unsigned m = a < b ? a : b;
  return m < c ? m : c;
}

static void
sFuzzyWalk(sTrieNode *n, sFuzzyCtx *ctx)
{
  unsigned *cur, *next;
  int       i;

  if (!n) return;
  cur  = ctx->rows + ctx->depth * (ctx->tlen + 1);
  next = cur + (ctx->tlen + 1);

  for (i = 0; i < n->n_edges; i++) {
    sTrieNode           *child = n->edges[i].child;
    unsigned char        b     = n->edges[i].byte;
    unsigned             best_in_row = (unsigned)-1;
    size_t               j;

    /* DEPTH IS BOUNDED, and the bound is what makes the row array a fixed size rather than an
       unbounded one. A path deeper than the target plus the threshold cannot be within the
       threshold -- every character past that point costs at least one deletion -- so the walk
       stops there, and the array needs one row per depth up to that. Without this the array is
       indexed past its end by any word longer than the prefix, which is every word in a trie
       that holds them. */
    if (ctx->depth + 1 > ctx->tlen + ctx->threshold) continue;

    ctx->depth++;
    // next[0] is the cost of matching a prefix of the target against nothing.
    next[0] = (unsigned)ctx->depth;
    for (j = 1; j <= ctx->tlen; j++) {
      unsigned cost = (sTrieFold((unsigned char)ctx->target[j - 1]) == b) ? 0u : 1u;
      next[j] = sFuzzyMin3(next[j - 1] + 1, cur[j] + 1, cur[j - 1] + cost);
      if (next[j] < best_in_row) best_in_row = next[j];
    }
    /* THE PRUNE, and it is the whole reason this is affordable: if every cell of the row
       already exceeds the threshold, no descendant of this node can be within it -- the row's
       values only grow or hold as the walk descends. */
    if (best_in_row <= ctx->threshold) {
      if (child->word) {
        unsigned d = next[ctx->tlen];
        // Ties are broken the way the exact descent breaks them: the heavier word wins, and an equal weight falls to the traversal order, which is lexicographic.
        int better = (d < ctx->best_dist) ||
                     (d == ctx->best_dist && ctx->best && child->weight > ctx->best->weight) ||
                     (d == ctx->best_dist && !ctx->best);
        if (d <= ctx->threshold && better) {
          ctx->best      = child;
          ctx->best_dist = d;
        }
      }
      sFuzzyWalk(child, ctx);
    }
    ctx->depth--;
  }
}

/* The best approximate completion of `prefix_ptr`, or NULL when nothing is within the
   threshold. */
static sTrieNode *
sTrieFuzzy(sTrieInstance *trie, const char *prefix_ptr, unsigned *dist_ptr)
{
  sFuzzyCtx ctx;
  size_t    tlen = strlen(prefix_ptr), j;
  unsigned *row0;

  if (!trie || !trie->root || trie->fuzzy == 0 || tlen == 0) return NULL;

  /* ONE BLOCK, SIZED BY THE DEPTH BOUND RATHER THAN BY THE PREFIX. The walk may descend to
     depth tlen + threshold and writes the row at depth+1 before deciding whether to go
     further, so the array needs tlen + threshold + 2 rows of (tlen + 1).
   *
     Sizing it by the prefix alone is the bug the first version shipped, and the probe's own
     ASan run found it in seconds: with a 3-character prefix and a threshold of 2 the walk
     reached depth 5 in a 5-row array and wrote one row past the end. It is written down here
     because the arithmetic is the kind that looks obviously right either way. */
  /* `trie->fuzzy` rather than `ctx.threshold` in the size, and that is not a style choice: the
     field is assigned three lines below, so reading it here would size the array for a
     threshold of zero. That is the second off-by-one this function shipped, and the probe's
     ASan run found it in seconds as well. */
  ctx.rows = calloc(tlen + (size_t)trie->fuzzy + 2, (tlen + 1) * sizeof *ctx.rows);
  if (!ctx.rows) return NULL;

  ctx.target = prefix_ptr;
  ctx.tlen   = tlen;
  ctx.threshold = (unsigned)trie->fuzzy;
  ctx.best   = NULL;
  ctx.best_dist = (unsigned)-1;
  ctx.depth  = 0;

  row0 = ctx.rows;
  for (j = 0; j <= tlen; j++) row0[j] = (unsigned)j;

  // One call: sFuzzyWalk walks a node's CHILDREN, so the root's call covers the first byte of every word, and the recursion covers the rest.
  sFuzzyWalk(trie->root, &ctx);
  if (!ctx.best) {
    free(ctx.rows);
    return NULL;
  }
  if (dist_ptr) *dist_ptr = ctx.best_dist;
  free(ctx.rows);
  return ctx.best;
}

static void
sTrieDestroy(void *instance_ptr)
{
  sTrieInstance *trie = instance_ptr;
  if (!trie) return;
  sTrieNodeFree(trie->root);
  free(trie);
}

PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateTrie(void)
{
  IrcitUiDictionary *dict;
  sTrieInstance     *trie;

  trie = calloc(1, sizeof *trie);
  if (!trie) return NULL;
  trie->root = sTrieNodeNew();
  if (!trie->root) {
    free(trie);
    return NULL;
  }
  trie->nodes = 1;

  dict = calloc(1, sizeof *dict);
  if (!dict) {
    sTrieDestroy(trie);
    return NULL;
  }
  dict->instance    = trie;
  dict->api.best    = sTrieQueryBest;
  dict->api.next    = sTrieQueryNth;
  dict->api.replace = sTrieReplace;
  dict->api.destroy = sTrieDestroy;
  return dict;
}

PUBLIC_API int
IrcitUiDictionaryInsert(IrcitUiDictionary *dictionary_ptr, const char *word_ptr,
                        const char *payload_ptr, int is_static)
{
  sTrieInstance *trie;
  sTrieNode     *n;

  if (!dictionary_ptr || !word_ptr || !word_ptr[0]) return 0;
  if (dictionary_ptr->api.best != sTrieQueryBest) return 0; /* not a trie: refused */
  trie = dictionary_ptr->instance;
  if (!trie) return 0;

  n = sTrieDescendEnsure(trie, word_ptr);
  if (!n) return 0;
  if (!sTrieSetWord(n, word_ptr)) return 0;
  // A re-insert updates the payload and the static flag rather than refusing: re-loading a file, or learning a word that a file already supplied, must not be an error.
  if (payload_ptr) {
    char *pcopy = malloc(strlen(payload_ptr) + 1);
    if (pcopy) {
      strcpy(pcopy, payload_ptr);
      free(n->payload);
      n->payload = pcopy;
    }
  }
  if (is_static) n->is_static = 1;
  return 1;
}

PUBLIC_API int
IrcitUiDictionarySetFuzzy(IrcitUiDictionary *dictionary_ptr, int threshold)
{
  sTrieInstance *trie;
  if (!dictionary_ptr || dictionary_ptr->api.best != sTrieQueryBest) return 0;
  trie = dictionary_ptr->instance;
  if (!trie) return 0;
  // Clamped rather than refused: a threshold above three stops being a typo tolerance and starts being a different word, and the cost of the walk grows with it.
  if (threshold < 0) threshold = 0;
  if (threshold > 3) threshold = 3;
  trie->fuzzy = threshold;
  return 1;
}

PUBLIC_API size_t
IrcitUiDictionarySize(IrcitUiDictionary *dictionary_ptr)
{
  sTrieInstance *trie;
  if (!dictionary_ptr || dictionary_ptr->api.best != sTrieQueryBest) return 0;
  trie = dictionary_ptr->instance;
  return trie ? trie->nodes : 0;
}

/* ---- the emoji tag table --------------------------------------------------- */

/* A tag and the glyph it inserts. THE TAG IS WHAT THE FIELD SHOWS and the glyph is what an
   accept writes -- which is why the interface needed a `replace` entry at all: the candidate
   the user reads is `:fire:` and the text they get is a fire, and a source that could only
   answer one of the two would have forced the widget to know about emoji. */
typedef struct
{
  const char *tag;
  const char *glyph;
} sEmojiEntry;

static const sEmojiEntry sEmojiTable[] = {
  { ":apple:",    "\U0001F34E" },
  { ":android:",  "\U0001F916" },
  { ":fire:",     "\U0001F525" },
  { ":heart:",    "\U0001F496" },
  { ":party:",    "\U0001F389" },
  { ":rocket:",   "\U0001F680" },
  { ":smile:",    "\U0001F604" },
  { ":star:",     "\U0001F31F" },
  { ":thumbsup:", "\U0001F44D" }
};
#define N_EMOJI (sizeof(sEmojiTable) / sizeof(sEmojiTable[0]))

/* The tag list is the instance, so the three entries below are the list's with a payload. */
static const char *const sEmojiTags[N_EMOJI] = {
  ":apple:", ":android:", ":fire:", ":heart:", ":party:", ":rocket:", ":smile:", ":star:", ":thumbsup:"
};

static int
sEmojiIndexOf(const char *tag_ptr)
{
  size_t i;
  if (!tag_ptr) return -1;
  for (i = 0; i < N_EMOJI; i++) if (strcmp(sEmojiTable[i].tag, tag_ptr) == 0) return (int)i;
  return -1;
}

/* The replacement rule, and the only implementation of it in this file: a candidate that is
   one of the table's tags is written as its glyph, and anything else gets no replacement --
   which the widget reads as "write the candidate's own tail". */
static int
sEmojiReplace(void *instance_ptr, const char *candidate_ptr, char *out_ptr, size_t out_sz)
{
  int i = sEmojiIndexOf(candidate_ptr);
  size_t n;
  (void)instance_ptr;
  if (i < 0 || !out_ptr || out_sz == 0) return 0;
  n = strlen(sEmojiTable[i].glyph);
  if (n + 1 > out_sz) n = out_sz - 1;
  memcpy(out_ptr, sEmojiTable[i].glyph, n);
  out_ptr[n] = 0;
  return 1;
}

PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateEmoji(void)
{
  IrcitUiDictionary *dict = IrcitUiDictionaryCreateList(sEmojiTags, N_EMOJI);
  if (!dict) return NULL;
  // The list does the matching; this is the whole of what makes it an emoji table.
  dict->api.replace = sEmojiReplace;
  return dict;
}

/* ---- the composite --------------------------------------------------------- */

#define MAX_COMPOSITE 4

typedef struct
{
  IrcitUiDictionary *chain[MAX_COMPOSITE];
  int                count;
} sCompositeInstance;

/* THE FIRST SOURCE THAT ANSWERS WINS, and a source that answers with nothing is not a veto:
   a composite of an emoji table and a word list must still complete "app" when the emoji
   table has no `:app:`. */
static int
sCompositeBest(void *instance_ptr, const char *prefix_ptr, char *out_ptr, size_t out_sz)
{
  sCompositeInstance *c = instance_ptr;
  int                 i;
  if (!c) return 0;
  for (i = 0; i < c->count; i++) {
    IrcitUiDictionary *d = c->chain[i];
    if (!d) continue;
    if (d->api.best && d->api.best(d->instance, prefix_ptr, out_ptr, out_sz)) return 1;
  }
  return 0;
}

/* ROUTED BY THE MEMBERS, not by the composite, and that is the only correct answer: the
   composite does not know which member produced the candidate, so it asks each in the order
   the chain names them and takes the first that recognises it. A source with no replacement
   rule declines, which is what makes a mixed chain -- a trie of shortcodes beside a word
   list -- behave. */
static int
sCompositeReplace(void *instance_ptr, const char *candidate_ptr, char *out_ptr, size_t out_sz)
{
  sCompositeInstance *c = instance_ptr;
  int                 i;
  if (!c) return 0;
  for (i = 0; i < c->count; i++) {
    IrcitUiDictionary *d = c->chain[i];
    if (!d || !d->api.replace) continue;
    if (d->api.replace(d->instance, candidate_ptr, out_ptr, out_sz)) return 1;
  }
  return 0;
}

static int
sCompositeNth(void *instance_ptr, const char *prefix_ptr, int ordinal, char *out_ptr, size_t out_sz)
{
  sCompositeInstance *c = instance_ptr;
  int                 i, seen = 0;
  char                cand[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ];

  if (!c) return 0;
  // The ordinals are flattened across the chain, so a list built from `next` walks the first source's candidates and then the second's, in the order the chain names them.
  for (i = 0; i < c->count; i++) {
    IrcitUiDictionary *d = c->chain[i];
    int                k = 0;
    if (!d || !d->api.next) continue;
    while (d->api.next(d->instance, prefix_ptr, k, cand, sizeof cand)) {
      if (seen == ordinal) {
        size_t n = strlen(cand);
        if (n + 1 > out_sz) n = out_sz - 1;
        memcpy(out_ptr, cand, n);
        out_ptr[n] = 0;
        return 1;
      }
      seen++;
      k++;
    }
  }
  return 0;
}

static void
sCompositeDestroy(void *instance_ptr)
{
  sCompositeInstance *c = instance_ptr;
  int                 i;
  if (!c) return;
  /* THE MEMBERS ARE NOT DESTROYED. They are borrowed, exactly as a field borrows the
     dictionary attached to it: a source may be in the chain of two composites, and a
     composite that freed its members would strand the other. The caller destroys each member
     after every composite naming it is gone. */
  for (i = 0; i < c->count; i++) c->chain[i] = NULL;
  free(c);
}

PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateComposite(IrcitUiDictionary *first_ptr, IrcitUiDictionary *second_ptr)
{
  sCompositeInstance *inst;
  IrcitUiDictionary  *dict;

  if (!first_ptr && !second_ptr) return NULL;
  inst = calloc(1, sizeof *inst);
  if (!inst) return NULL;
  if (first_ptr) inst->chain[inst->count++] = first_ptr;
  if (second_ptr && inst->count < MAX_COMPOSITE) inst->chain[inst->count++] = second_ptr;

  dict = calloc(1, sizeof *dict);
  if (!dict) {
    free(inst);
    return NULL;
  }
  dict->instance    = inst;
  dict->api.best    = sCompositeBest;
  dict->api.next    = sCompositeNth;
  dict->api.replace = sCompositeReplace;
  dict->api.destroy = sCompositeDestroy;
  return dict;
}

PUBLIC_API int
IrcitUiDictionaryChain(IrcitUiDictionary *composite_ptr, IrcitUiDictionary *source_ptr)
{
  sCompositeInstance *c;
  if (!composite_ptr || !source_ptr) return 0;
  if (composite_ptr->api.best != sCompositeBest) return 0; /* not a composite: refused */
  c = composite_ptr->instance;
  if (!c || c->count >= MAX_COMPOSITE) return 0;
  c->chain[c->count++] = source_ptr;
  return 1;
}

/* ---- learning, and forgetting ---------------------------------------------- */

PUBLIC_API int
IrcitUiDictionaryLearn(IrcitUiDictionary *dictionary_ptr, const char *word_ptr)
{
  sTrieNode *n;

  if (!dictionary_ptr || !word_ptr || !word_ptr[0]) return 0;
  if (dictionary_ptr->api.best != sTrieQueryBest) return 0; /* not a trie: refused */

  /* A word that was never inserted is inserted by being learned -- that is the whole point of
     learning from what the user types, and it is why this shares the walk with Insert rather
     than requiring one. It shares the COUNT too, which the first version did not. */
  n = sTrieDescendEnsure(dictionary_ptr->instance, word_ptr);
  if (!n) return 0;
  if (!sTrieSetWord(n, word_ptr)) return 0;
  // SATURATING, because a weight is a tie-break and not a statistic: a word typed a million times must not wrap to a low number and lose to one typed twice.
  if (n->weight < 0xFFFFFFFFu) n->weight++;
  return 1;
}

/* Post-order: drop the entries this subtree no longer justifies, and say whether it is worth
   keeping at all.

   THE STATIC FLAG IS THE WHOLE OF THE POLICY. A word that came from a file or from the emoji
   table is not the user's to lose; one that was learned is, once it stops being used. The
   design document's section 9 also offers `frequency_weight == 0` as a second way to mean
   "system entry", and that is refused here: two ways to say one thing is two ways to disagree,
   and a pruned entry is exactly a zero-weight one. */
static int
sTriePruneNode(sTrieNode *n, unsigned threshold, size_t *pruned)
{
  int i, keep = 0;

  if (!n) return 0;
  if (n->word) {
    if (n->is_static || n->weight >= threshold) {
      keep = 1;
    }
    else {
      free(n->word);
      free(n->payload);
      n->word    = NULL;
      n->payload = NULL;
      (*pruned)++;
    }
  }
  for (i = 0; i < n->n_edges;) {
    if (sTriePruneNode(n->edges[i].child, threshold, pruned)) {
      keep = 1;
      i++;
    }
    else {
      sTrieNodeFree(n->edges[i].child);
      memmove(&n->edges[i], &n->edges[i + 1], (size_t)(n->n_edges - i - 1) * sizeof *n->edges);
      n->n_edges--;
    }
  }
  return keep;
}

static size_t
sTrieCount(const sTrieNode *n)
{
  size_t total = 1;
  int    i;
  for (i = 0; i < n->n_edges; i++) total += sTrieCount(n->edges[i].child);
  return total;
}

PUBLIC_API size_t
IrcitUiDictionaryPrune(IrcitUiDictionary *dictionary_ptr, unsigned min_weight)
{
  sTrieInstance *trie;
  size_t         pruned = 0;

  if (!dictionary_ptr || dictionary_ptr->api.best != sTrieQueryBest) return 0;
  trie = dictionary_ptr->instance;
  if (!trie || !trie->root) return 0;

  // The root's own answer is discarded: a root with nothing under it is still the tree a later insert descends from.
  (void)sTriePruneNode(trie->root, min_weight, &pruned);
  /* RECOUNTED RATHER THAN DECREMENTED. Subtracting the words pruned would be wrong -- pruning
     a word can free every node on its path -- and the document's own section 9 admits its
     version is "a general approximation". A count that is only approximately right is worse
     than one that costs a walk, because the walk is the only thing that can be checked. */
  trie->nodes = sTrieCount(trie->root);
  return pruned;
}

/* ---- reading a word list off disk ------------------------------------------ */

/* The longest line this reader will look at, which is the caller's cap rather than a constant:
   a word list is someone else's file and its lines are the one thing about it that is not
   under this library's control. A line longer than the cap is REFUSED AND COUNTED rather than
   truncated -- half a word in a dictionary is a candidate nobody can type. */
PUBLIC_API size_t
IrcitUiDictionaryIngestFile(IrcitUiDictionary *dictionary_ptr, const char *path_ptr,
                            size_t max_word_length, size_t *refused_ptr)
{
  FILE  *f;
  char   line[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ];
  size_t added = 0, refused = 0;

  if (refused_ptr) *refused_ptr = 0;
  if (!dictionary_ptr || !path_ptr) return 0;
  if (dictionary_ptr->api.best != sTrieQueryBest) return 0; /* not a trie: refused */

  f = fopen(path_ptr, "r");
  if (!f) {
    /* NOT AN ERROR WORTH FAILING ON. A dictionary file that is not there means the source
       contributes nothing, which is a state the field already handles -- it is what "no
       dictionary" looks like -- and a library that refused to start over a missing optional
       file would be a worse neighbour than one that logs. */
    IRCITUI_LOG_WARN("dictionary: %s could not be opened; the source contributes nothing", path_ptr);
    return 0;
  }

  while (fgets(line, sizeof line, f)) {
    size_t n = strlen(line);
    // A line without a newline that filled the buffer is a line longer than this reader can hold, and it is refused rather than ingested as a prefix of itself.
    if (n == sizeof line - 1 && line[n - 1] != '\n') {
      int c;
      while ((c = fgetc(f)) != EOF && c != '\n') { /* discard the rest of the line */ }
      refused++;
      continue;
    }
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (n == 0) continue;
    /* A `#` line is a comment. The convention rather than an invention: a checked-in word
       list needs a way to say what it is and why it is shaped the way it is, and the
       alternative is a file whose every explanation becomes a candidate. */
    if (line[0] == '#') continue;
    if (max_word_length && n > max_word_length) {
      refused++;
      continue;
    }
    if (IrcitUiDictionaryInsert(dictionary_ptr, line, NULL, 1)) added++;
    else refused++;
  }
  fclose(f);
  // The count is how many are in the trie now, which for a file that names a word twice is fewer than the lines read -- Insert updates rather than duplicating.
  if (refused_ptr) *refused_ptr = refused;
  return added;
}

/* ---- persistence ------------------------------------------------------------ */

/* The record, as the design document's section 10 specifies it:
 *
 *     [4 bytes: weight] [2 bytes: length] [N bytes: the word]
 *
 * WRITTEN BYTE BY BYTE RATHER THAN AS A STRUCT, so the file means the same thing on a machine
 * that disagrees about padding or byte order. A struct dumped with fwrite is a file that reads
 * back only where it was written, and the whole point of persisting a learned profile is that
 * it outlives the session that made it.
 *
 * WHAT IS WRITTEN: the learned entries only. A static entry came from a file or from the
 * compiled-in table and will be there again next time -- writing it down would mean a profile
 * that could disagree with the source it came from. */
static int
sPutU32(FILE *f, unsigned v)
{
  unsigned char b[4];
  b[0] = (unsigned char)(v & 0xFFu);
  b[1] = (unsigned char)((v >> 8) & 0xFFu);
  b[2] = (unsigned char)((v >> 16) & 0xFFu);
  b[3] = (unsigned char)((v >> 24) & 0xFFu);
  return fwrite(b, 1, 4, f) == 4;
}

static int
sPutU16(FILE *f, unsigned v)
{
  unsigned char b[2];
  b[0] = (unsigned char)(v & 0xFFu);
  b[1] = (unsigned char)((v >> 8) & 0xFFu);
  return fwrite(b, 1, 2, f) == 2;
}

static void
sTrieWriteNode(const sTrieNode *n, FILE *f, size_t *written)
{
  int i;
  if (!n) return;
  if (n->word && !n->is_static && n->weight > 0) {
    size_t len = strlen(n->word);
    if (len <= 0xFFFFu && sPutU32(f, n->weight) && sPutU16(f, (unsigned)len) &&
        fwrite(n->word, 1, len, f) == len) {
      (*written)++;
    }
  }
  for (i = 0; i < n->n_edges; i++) sTrieWriteNode(n->edges[i].child, f, written);
}

PUBLIC_API size_t
IrcitUiDictionaryExport(IrcitUiDictionary *dictionary_ptr, const char *path_ptr)
{
  sTrieInstance *trie;
  FILE          *f;
  size_t         written = 0;
  unsigned       magic;

  if (!dictionary_ptr || !path_ptr) return 0;
  if (dictionary_ptr->api.best != sTrieQueryBest) return 0;
  trie = dictionary_ptr->instance;
  if (!trie) return 0;

  f = fopen(path_ptr, "wb");
  if (!f) {
    IRCITUI_LOG_WARN("dictionary: %s could not be written; nothing was saved", path_ptr);
    return 0;
  }
  /* A FOUR-BYTE MARKER FIRST, so a file that is not one of ours is refused at the door rather
     than parsed as a sequence of lengths. The length field is read from the file and is
     therefore attacker-controlled input from disk; the marker does not make that safe, but it
     makes an accidental mismatch loud. */
  magic = 0x55544931u; /* "UIT1" */
  sPutU32(f, magic);
  sTrieWriteNode(trie->root, f, &written);
  fclose(f);
  return written;
}

PUBLIC_API size_t
IrcitUiDictionaryImport(IrcitUiDictionary *dictionary_ptr, const char *path_ptr)
{
  sTrieInstance *trie;
  FILE          *f;
  size_t         loaded = 0;
  unsigned char  hdr[4];

  if (!dictionary_ptr || !path_ptr) return 0;
  if (dictionary_ptr->api.best != sTrieQueryBest) return 0;
  trie = dictionary_ptr->instance;
  if (!trie) return 0;

  f = fopen(path_ptr, "rb");
  if (!f) {
    // Not an error, for the reason IngestFile gives: a profile that is not there yet is the FIRST run, not a failure.
    IRCITUI_LOG_INFO("dictionary: no profile at %s; starting empty", path_ptr);
    return 0;
  }
  if (fread(hdr, 1, 4, f) != 4) {
    fclose(f);
    IRCITUI_LOG_WARN("dictionary: %s is too short to be a profile", path_ptr);
    return 0;
  }
  if ((hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((unsigned)hdr[3] << 24)) != 0x55544931u) {
    fclose(f);
    IRCITUI_LOG_WARN("dictionary: %s is not a profile (bad marker); refused", path_ptr);
    return 0;
  }

  for (;;) {
    unsigned char wb[4], lb[2];
    unsigned      weight, len;
    char         *word;
    sTrieNode    *node;

    if (fread(wb, 1, 4, f) != 4) break;   /* clean end, or a truncated tail: both stop here */
    if (fread(lb, 1, 2, f) != 2) break;
    weight = wb[0] | (wb[1] << 8) | ((unsigned)wb[2] << 16) | ((unsigned)wb[3] << 24);
    len    = lb[0] | ((unsigned)lb[1] << 8);
    if (len == 0 || len > 4096) break;    /* a length no word has: the file is not ours */

    word = malloc((size_t)len + 1);
    if (!word) break;
    if (fread(word, 1, len, f) != len) {
      free(word);
      break;
    }
    word[len] = 0;

    /* A WORD WITH AN EMBEDDED NUL WOULD BE A WORD OF A DIFFERENT LENGTH THAN THE FILE SAYS.
       Refused rather than truncated, because the record's length and the string's length
       disagreeing is the signature of a file that is not what it claims. */
    if (strlen(word) != len) {
      free(word);
      break;
    }
    node = sTrieDescendEnsure(trie, word);
    if (!node || !sTrieSetWord(node, word)) {
      free(word);
      break;
    }
    // THE WEIGHT IS RESTORED, not re-earned: this is the whole of what the profile is for.
    node->weight = weight;
    free(word);
    loaded++;
  }
  fclose(f);
  return loaded;
}

/* ---- the verbs the host calls ---------------------------------------------- */

PUBLIC_API void
IrcitUiDictionaryDestroy(IrcitUiDictionary *dictionary_ptr)
{
  if (!dictionary_ptr) return;
  // Through the vtable, so a host's own dictionary releases its own instance its own way. A NULL destroy is legal and means the host frees it itself.
  if (dictionary_ptr->api.destroy) dictionary_ptr->api.destroy(dictionary_ptr->instance);
  dictionary_ptr->instance = NULL;
  free(dictionary_ptr);
}
