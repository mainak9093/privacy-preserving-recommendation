# `references/` — the surveyed papers

PDFs for works cited in [`../report/references.bib`](../report/references.bib).
**40 of 66 obtained** (~38 MB). Filenames match the BibTeX citation key
(`grotto2023.pdf` <-> `\cite{grotto2023}`), except four downloaded by hand before that
convention existed.

---

## How these were verified — and what went wrong first

Acquisition was automated: DOI -> Semantic Scholar open-access lookup, then IACR ePrint title
search, then arXiv title search.

**The first pass silently downloaded 11 wrong papers.** Title-similarity matching is not a
sufficient check, because a *superset* title passes trivially. "Content-boosted Matrix
Factorization Techniques for Recommender Systems" contains every word of Koren et al.'s title;
"From Lattices to Tensor Cores: Accelerating Private Information Retrieval" contains every word
of Chor et al.'s. Both were downloaded, and both were the wrong paper.

**Every file here now passes an author check**: at least half the author surnames in the BibTeX
entry must appear on page 1 of the PDF. Authors discriminate where titles do not — this caught
all 11. The wrong files were deleted, not renamed.

Where the published venue is paywalled (ACM DL, IEEE Xplore) the local copy is the author's
ePrint or arXiv version; **the citation is always the published version**, which DBLP verified.
One consequence worth noting: `narayanan2008.pdf` is the arXiv preprint, whose title differs
from the S&P 2008 version.

---

## Obtained (40)

| Key | Paper | Venue | Year |
|---|---|---|---|
| `aby22021` | ABY2.0: Improved Mixed-Protocol Secure Two-Party Computation | 30th USENIX Security Symposium | 2021 |
| `ammadudin2019` | Federated Collaborative Filtering for Privacy-Preserving Personali | CoRR | 2019 |
| `asharov2018` | Privacy-Preserving Search of Similar Patients in Genomic Data | Proc. Priv. Enhancing Technol. | 2018 |
| `bgi2016` | Function Secret Sharing: Improvements and Extensions | Proceedings of the 2016 ACM SIGSAC Confe | 2016 |
| `boyle2021` | Function Secret Sharing for Mixed-Mode and Fixed-Point Secure Comp | Advances in Cryptology - EUROCRYPT 2021  | 2021 |
| `cgk2020` | Private Information Retrieval with Sublinear Online Time | Advances in Cryptology - EUROCRYPT 2020  | 2020 |
| `chai2021` | Secure Federated Matrix Factorization | IEEE Intell. Syst. | 2021 |
| `compass2025` | Compass: Encrypted Semantic Search with High Accuracy | 19th USENIX Symposium on Operating Syste | 2025 |
| `dacrema2019` | Are we really making much progress? A worrying analysis of recent  | Proceedings of the 13th ACM Conference o | 2019 |
| `duoram2023` | Duoram: A Bandwidth-Efficient Distributed ORAM for 2- and 3-Party  *(hand-downloaded: `DUORAM.pdf`)* | 32nd USENIX Security Symposium | 2023 |
| `escudero2020` | Improved Primitives for MPC over Mixed Arithmetic-Binary Circuits | Advances in Cryptology - CRYPTO 2020 - 4 | 2020 |
| `floram2017` | Scaling ORAM for Secure Computation | Proceedings of the 2017 ACM SIGSAC Confe | 2017 |
| `gentry2009` | Fully homomorphic encryption using ideal lattices | Proceedings of the 41st Annual ACM Sympo | 2009 |
| `grotto2023` | Grotto: Screaming fast (2+1)-PC or (mathbbZ)2n via (2, 2)-DPFs *(hand-downloaded: `Grotto.pdf`)* | Proceedings of the 2023 ACM SIGSAC Confe | 2023 |
| `hafiz2019` | A Bit More Than a Bit Is More Than a Bit Better: Faster (essential | Proc. Priv. Enhancing Technol. | 2019 |
| `hamlin2021` | Two-Server Distributed ORAM with Sublinear Computation and Constan | Public-Key Cryptography - PKC 2021 - 24t | 2021 |
| `jarecki2018` | 3PC ORAM with Low Latency, Low Bandwidth, and Fast Batch Retrieval | Applied Cryptography and Network Securit | 2018 |
| `liang2018` | Variational Autoencoders for Collaborative Filtering | Proceedings of the 2018 World Wide Web C | 2018 |
| `lindell2017` | How to Simulate It - A Tutorial on the Simulation Proof Technique | Tutorials on the Foundations of Cryptogr | 2017 |
| `llama2022` | LLAMA: A Low Latency Math Library for Secure Inference | Proc. Priv. Enhancing Technol. | 2022 |
| `lu2013` | Distributed Oblivious RAM for Secure Two-Party Computation | Theory of Cryptography - 10th Theory of  | 2013 |
| `mpspdz2020` | MP-SPDZ: A Versatile Framework for Multi-Party Computation | CCS '20: 2020 ACM SIGSAC Conference on C | 2020 |
| `narayanan2008` | Robust De-anonymization of Large Sparse Datasets | 2008 IEEE Symposium on Security and Priv | 2008 |
| `nudge2026` | Nudge: A Private Recommendations Engine *(hand-downloaded: `Nudge A Private Recommendations Engine.pdf`)* | 35th USENIX Security Symposium | 2026 |
| `nudge2026eprint` | Nudge: A Private Recommendations Engine | IACR Cryptol. ePrint Arch. | 2026 |
| `pacmann2025` | Pacmann: Efficient Private Approximate Nearest Neighbor Search | The Thirteenth International Conference  | 2025 |
| `panther2025` | Panther: Private Approximate Nearest Neighbor Search in the Single | Proceedings of the 2025 ACM SIGSAC Confe | 2025 |
| `pathoram2013` | Path ORAM: an extremely simple oblivious RAM protocol | 2013 ACM SIGSAC Conference on Computer a | 2013 |
| `pika2022` | Pika: Secure Computation using Function Secret Sharing over Rings | Proc. Priv. Enhancing Technol. | 2022 |
| `pirsona2021` | You May Also Like... Privacy: Recommendation Systems Meet PIR *(hand-downloaded: `Recommendation System PIR.pdf`)* | Proc. Priv. Enhancing Technol. | 2021 |
| `prac2024` | PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures | Proc. Priv. Enhancing Technol. | 2024 |
| `sanns2020` | SANNS: Scaling Up Secure Approximate k-Nearest Neighbors Search | 29th USENIX Security Symposium | 2020 |
| `sealpir2018` | PIR with Compressed Queries and Amortized Query Processing | 2018 IEEE Symposium on Security and Priv | 2018 |
| `secureml2017` | SecureML: A System for Scalable Privacy-Preserving Machine Learnin | 2017 IEEE Symposium on Security and Priv | 2017 |
| `securenn2019` | SecureNN: 3-Party Secure Computation for Neural Network Training | Proc. Priv. Enhancing Technol. | 2019 |
| `simplepir2023` | One Server for the Price of Two: Simple and Fast Single-Server Pri | 32nd USENIX Security Symposium | 2023 |
| `spiral2022` | SPIRAL: Fast, High-Rate Single-Server PIR via FHE Composition | 43rd IEEE Symposium on Security and Priv | 2022 |
| `tiptoe2023` | Private Web Search with Tiptoe | Proceedings of the 29th Symposium on Ope | 2023 |
| `wally2024` | Scalable Private Search with Wally | CoRR | 2024 |
| `wang2014` | Oblivious Data Structures | Proceedings of the 2014 ACM SIGSAC Confe | 2014 |

---

## Not obtained (26)

Paywalled with no open version found, or removed by the audit above. **These remain cited** —
their metadata is DBLP-verified — but they are Tier C in
[`../report/notes/evidence.md`](../report/notes/evidence.md): they appear in the taxonomy and the
reference list, and the survey makes no claim about their contents that is not attributed to a
source we did read.

| Key | Paper | Venue | Year |
|---|---|---|---|
| `aby2015` | ABY - A Framework for Efficient Mixed-Protocol Secure Two-Party Co | 22nd Annual Network and Distributed Syst | 2015 |
| `aby32018` | ABY(^mbox3): A Mixed Protocol Framework for Machine Learning | Proceedings of the 2018 ACM SIGSAC Confe | 2018 |
| `araki2016` | High-Throughput Semi-Honest Secure Three-Party Computation with an | Proceedings of the 2016 ACM SIGSAC Confe | 2016 |
| `beaver1991` | Efficient Multiparty Protocols Using Circuit Randomization | Advances in Cryptology - CRYPTO '91 | 1991 |
| `bgi2015` | Function Secret Sharing | Advances in Cryptology - EUROCRYPT 2015  | 2015 |
| `bunn2020` | Efficient 3-Party Distributed ORAM | Security and Cryptography for Networks - | 2020 |
| `calandrino2011` | "You Might Also Like: " Privacy Risks of Collaborative Filtering | 32nd IEEE Symposium on Security and Priv | 2011 |
| `chor1995` | Private Information Retrieval | 36th Annual Symposium on Foundations of  | 1995 |
| `dpfsurvey2026` | Distributed Point Functions and Function Secret Sharing | CoRR | 2026 |
| `dwork2014` | The Algorithmic Foundations of Differential Privacy | Found. Trends Theor. Comput. Sci. | 2014 |
| `evans2018` | A Pragmatic Introduction to Secure Multi-Party Computation | Found. Trends Priv. Secur. | 2018 |
| `gilboa2014` | Distributed Point Functions and Their Applications | Advances in Cryptology - EUROCRYPT 2014  | 2014 |
| `goldreich1996` | Software Protection and Simulation on Oblivious RAMs | J. ACM | 1996 |
| `hu2008` | Collaborative Filtering for Implicit Feedback Datasets | Proceedings of the 8th IEEE Internationa | 2008 |
| `ishai2006` | Cryptography from Anonymity | 47th Annual IEEE Symposium on Foundation | 2006 |
| `koren2009` | Matrix Factorization Techniques for Recommender Systems | Computer | 2009 |
| `kushilevitz1997` | Replication is NOT Needed: SINGLE Database, Computationally-Privat | 38th Annual Symposium on Foundations of  | 1997 |
| `mcsherry2009` | Differentially Private Recommender Systems: Building Privacy into  | Proceedings of the 15th ACM SIGKDD Inter | 2009 |
| `movielens2016` | The MovieLens Datasets: History and Context | ACM Trans. Interact. Intell. Syst. | 2016 |
| `nikolaenko2013` | Privacy-preserving matrix factorization | 2013 ACM SIGSAC Conference on Computer a | 2013 |
| `paillier1999` | Public-Key Cryptosystems Based on Composite Degree Residuosity Cla | Advances in Cryptology - EUROCRYPT '99 | 1999 |
| `sabre2022` | Sabre: Sender-Anonymous Messaging with Fast Audits | 43rd IEEE Symposium on Security and Priv | 2022 |
| `servanschreiber2022` | Private Approximate Nearest Neighbor Search with Sublinear Communi | 43rd IEEE Symposium on Security and Priv | 2022 |
| `shamir1979` | How to Share a Secret | Commun. ACM | 1979 |
| `shmueli2017` | Secure Multi-Party Protocols for Item-Based Collaborative Filterin | Proceedings of the Eleventh ACM Conferen | 2017 |
| `yao1986` | How to Generate and Exchange Secrets (Extended Abstract) | 27th Annual Symposium on Foundations of  | 1986 |
