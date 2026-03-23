# Consolidated List of Ian Gent’s Contributions



---

These are presented in approximate chronological order.  Level of heading indicates how Happy I was on them! 

Document produced in part by DeepSeek on 23 March 2026

## 0  Lossiness and surprising non requirements.

From a prompt: 

> As well as using the fact that cards can be in their starting position, cards can be in their final position, so this might be a useful insight. We can assume we know the original layout. The cache will only be used for one run so it is unnecessary to store details which distinguish between different layouts. The only two properties we must have are. 1. From a given game layout we get to the same bit sequence - and preferably the same bit sequence from two symmetric game sequence. 2. Two non equivalent game layouts that could possibly be played from the starting position must lead to different bit sequences. This leaves some properties that, surprisingly, we do NOT need to worry about in the encoding for storing in cache, as long as the above properties are retained. These might give scope for possibilities for even higher state compression. It is NOT required that from a bit sequence we can reconstruct the game layout, as we never have to do that - just compare two game layouts. The reduction to bit sequence can be lossy. Also it is NOT required that two different bit sequences must encode different game layouts. I.e. more than one bit sequence can represent the same game layout, as long as the algorithm we implement always chooses the same one. This may not be useful but it could allow optimisations since we can CHOOSE which representation to use if there is more than one. E.g. we choose one if there are fewer than 10 cards on tableau and another if there are more, with one bit selecting which we are using. Also is NOT required that a bit sequence must represent a legal state. We will not create such states so we do not need to ensure e.g.\that a Klondike layout uses no more than 7 piles. 

### 1. Proposal to use Abstract Zobrist Hashing in a general way
**Insight**: Hash should work for any Solitaire variant, not optimised for a single game.  
**Evidence**: *AI_Docs-4.pdf* – Prompt 1.


### 2. Unified single‑cache design
**Insight**: False negatives on cycle detection are acceptable; a single flat cache with insertion on forward visit, no live bits, and no separate path structure suffices.  
**Evidence**: *caching_survey_v2.pdf* – Section 7 (AI Generation Statement and Section 7); 
*AI_Docs-4.pdf* – Prompt 4.


### 3. Removal of live bits and insertion on forward visit
**Insight**: Removing live‑bit overhead and inserting states on forward visit (rather than on backtrack) enables the unified cache design.  
**Evidence**: *AI_Docs-4.pdf* – Prompt 4.


# 4. Starting position covers stock, waste, and reserve
**Insight**: Cards in stock, waste, or reserve are in their starting positions and require no per‑card encoding; a waste pointer determines the stock/waste configuration.  
**Evidence**: *AI_Docs-3.pdf* – Section 2.1; *AI_Docs-5.pdf* – Section 3.1; *payload_spec.pdf* – AI Generation Statement.


### 5. Encoding constraints and optimisations (collated descriptors, scheme selection, etc.)
**Insight**: Use small fixed‑width fields per card; encoding does not need to be reconstructible; multiple representations with a selector bit can improve average case.  
**Evidence**: *AI_Docs-4.pdf* – Prompt 7.

# 6. Root cards need no pile identifier
**Insight**: A face‑up root card at the bottom of a tableau pile can be encoded simply as “root”; its association with face‑down blocks is recoverable from the initial layout and starting‑position information.  
**Evidence**: *AI_Docs-3.pdf* – Section 2.2; *AI_Docs-5.pdf* – Section 4.1.

### 7. Built groups: only the top card moves
**Insight**: When a built group moves as a unit, only the top card needs its descriptor updated; internal parent relationships are unchanged.  
**Evidence**: *AI_Docs-3.pdf* – Section 2.4; *AI_Docs-5.pdf* – Section 4.

## 8. Bottom‑up chain perspective
**Insight**: Viewing encoding from roots upward shows that when many cards are on the tableau most are chain members costing few bits, and when few are on the tableau the total encoding is small.  
**Evidence**: *AI_Docs-3.pdf* – Section 4.


# 9. Card‑centric encoding eliminates pile‑symmetry sorting
**Insight**: By encoding each card’s location rather than each pile’s contents, and using descriptors that never reference pile indices, the encoding becomes inherently canonical under pile permutation.  
**Evidence**: *AI_Docs-5.pdf* – AI Generation Statement, Section 1, and passim.

## 10. Descriptor‑aligned Zobrist hashing
**Insight**: Zobrist hash should be aligned with per‑card descriptor values from the payload, not with pile roles or positions.  
**Evidence**: *descriptor_zobrist_revised.pdf* – AI Generation Statement, Section 2, and throughout.

### 11. Elimination of the two‑layer Zobrist combining scheme
**Insight**: With descriptor‑aligned keys, per‑pile hashes, additive combining for interchangeable piles, and the distinction between interchangeable/non‑interchangeable piles become unnecessary.  
**Evidence**: *descriptor_zobrist_revised.pdf* – Section 3.1.

# 12. Dramatic reduction of the Zobrist table size
**Insight**: Table reduces from ~295 KB to ~4 KB, fitting in L1 cache.  
**Evidence**: *descriptor_zobrist_revised.pdf* – Section 3.2.

## 13. Merging of hash and payload maintenance
**Insight**: Because both are keyed by the same event (descriptor change), they can be updated in the same code path, simplifying incremental updates.  
**Evidence**: *descriptor_zobrist_revised.pdf* – Section 3.3.

### 14. Introduction of the `STARTING_FACE_UP` descriptor
**Insight**: Resolves ambiguity where a face‑down card that has been revealed but not moved still had the `STARTING` descriptor.  
**Evidence**: *descriptor_zobrist_revised.pdf* – Section 4..

# 15. Metamorphic cache testing insight
**Insight**: During refactoring, two independent cache implementations see the same operation stream, enabling cross‑validation up to the first eviction.  
**Evidence**: *human_contributions.md* – Section 2.

# 16. 1 bit payload

**Insight**: When we are streamlining, we need not worry about false positives in cache so do not need to store payload confirming equivalence of state.  We only need one bit for whether or not hash value has been seen. Benefit is increasing number of states in cache by 256 times compared to 32 byte payload.

---

## Source Documents and Abbreviations

| Abbreviation in references | Full document name |
|----------------------------|--------------------|
| [desc_zobrist] | *descriptor_zobrist_revised.pdf* |
| [caching_survey_v2] | *caching_survey_v2.pdf* |
| [state_comp_v1] | *AI_Docs-3.pdf* (General State Compression for ReSolvitaire: A Per‑Card Descriptor Encoding) |
| [payload_spec] | *payload_spec.pdf* |
| [prompt_log] | *AI_Docs-4.pdf* (Prompt Log) |
| [human_contrib] | *human_contributions.md* |
| [state_comp_v2] | *AI_Docs-5.pdf* (State Compression for ReSolitaire: A Card‑Centric Encoding that Eliminates Sorting) |

*All documents were provided in the conversation and are assumed to be accurate records of the collaboration.*

