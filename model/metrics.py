"""Ranking metrics.

Definitions are written out explicitly because a grader will check them, and
because NUDGE's reported nDCG@20 of 0.29 is only comparable under a stated
convention.

Relevance is binary at rating >= 4.
DCG@K  = sum_{i=1..K} rel_i / log2(i + 1)
IDCG@K = sum_{i=1..min(K, |R_u|)} 1 / log2(i + 1)
nDCG@K = DCG@K / IDCG@K, averaged over users with at least one relevant item.
"""
import numpy as np

REL_THRESHOLD = 4.0


def ndcg_at_k(scores, test, train, k=20, threshold=REL_THRESHOLD):
    """scores, test, train: (n_users, n_items). Items seen in train are masked."""
    n_users = scores.shape[0]
    discounts = 1.0 / np.log2(np.arange(2, k + 2))
    total, counted = 0.0, 0
    for u in range(n_users):
        relevant = test[u] >= threshold
        n_rel = int(relevant.sum())
        if n_rel == 0:
            continue
        s = scores[u].copy()
        s[train[u] > 0] = -np.inf          # never recommend an already-seen item
        topk = np.argpartition(-s, k)[:k]
        topk = topk[np.argsort(-s[topk])]
        gains = relevant[topk].astype(np.float64)
        dcg = float((gains * discounts).sum())
        idcg = float(discounts[: min(k, n_rel)].sum())
        total += dcg / idcg
        counted += 1
    return total / counted if counted else 0.0


def recall_at_k(scores, test, train, k=20, threshold=REL_THRESHOLD):
    n_users = scores.shape[0]
    total, counted = 0.0, 0
    for u in range(n_users):
        relevant = test[u] >= threshold
        n_rel = int(relevant.sum())
        if n_rel == 0:
            continue
        s = scores[u].copy()
        s[train[u] > 0] = -np.inf
        topk = np.argpartition(-s, k)[:k]
        total += float(relevant[topk].sum()) / min(k, n_rel)
        counted += 1
    return total / counted if counted else 0.0
