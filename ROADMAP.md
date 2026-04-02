# A.E.T.H.E.R. Development Roadmap

## Phase 1: Foundation (Current) ✅
**Status:** Complete

- [x] Python prototype for protocol validation
- [x] Basic DHT key-value storage
- [x] Peer management and discovery
- [x] CLI interface
- [x] Web dashboard
- [x] Unit tests (55 passing)

**Deliverable:** `aether-python` - Functional prototype for testing protocol concepts

---

## Phase 2: Hyper-Core (C Implementation)
**Status:** Planned

### 2.1 Core Daemon Rewrite
- [ ] Single dependency-free C binary
- [ ] io_uring (Linux) / IOCP (Windows) for async I/O
- [ ] Zero-copy networking
- [ ] Connection handling: 10,000+ peers

### 2.2 Kernel Integration
- [ ] eBPF packet processor (Linux)
- [ ] Direct kernel memory serving for popular fragments
- [ ] User-space fallback for complex requests

### 2.3 Hardware Crypto Acceleration
- [ ] CPU feature detection (AVX-512, ARM Neon)
- [ ] SHA-3 hardware instructions
- [ ] Ed25519 signature verification offload

**Milestone:** `aether-core` - Sub-millisecond latency, 10K+ connections

---

## Phase 3: Holographic Storage
**Status:** Planned

### 3.1 Erasure Coding
- [ ] Reed-Solomon encoding/decoding
- [ ] Configurable fragment ratio (e.g., 100/200)
- [ ] Fragment distribution across peers

### 3.2 Data Permanence
- [ ] Merkle tree for fragment verification
- [ ] Automatic fragment regeneration
- [ ] Redundancy monitoring and repair

### 3.3 Content Addressing
- [ ] SHA-3 content hashing
- [ ] Merkle root binding for manifests
- [ ] Immutable content references

**Milestone:** Mathematically guaranteed data recovery

---

## Phase 4: Semantic DHT
**Status:** Research

### 4.1 Edge Vectorization
- [ ] Embedded quantized AI model (llama.cpp integration)
- [ ] Local vector embedding generation
- [ ] Batch processing for bulk content

### 4.2 Semantic Routing
- [ ] Vector-based DHT key derivation
- [ ] Proximity routing by semantic similarity
- [ ] Query routing without central index

### 4.3 Search Interface
- [ ] Natural language query processing
- [ ] Result ranking by semantic relevance
- [ ] Trust-weighted results

**Milestone:** AI-native search without central index

---

## Phase 5: Super-Sidecar (Wasm Runtime)
**Status:** Planned

### 5.1 Wasm Execution Engine
- [ ] wasmtime or wasmer integration
- [ ] Secure sandboxing
- [ ] Resource limits (CPU, memory, network)

### 5.2 CRDT Implementation
- [ ] LWW-Register (Last-Writer-Wins)
- [ ] G-Set, PN-Counter
- [ ] CRDT-based collaborative documents
- [ ] Real-time sync protocol

### 5.3 Application Framework
- [ ] App manifest format
- [ ] Dependency resolution
- [ ] Local state persistence

**Milestone:** Full decentralized applications on A.E.T.H.E.R.

---

## Phase 6: Aegis Browser
**Status:** Design

### 6.1 Browser Core
- [ ] Tauri-based (Rust + WebView)
- [ ] A.E.T.H.E.R. Hyper-Core integration
- [ ] Request racing (Web2 vs A.E.T.H.E.R.)
- [ ] Ad/tracker blocking

### 6.2 Proof of Attention
- [ ] Engagement tracking (time, scroll)
- [ ] Content prioritization
- [ ] Silent archival pipeline

### 6.3 Ghost Storage
- [ ] Sparse file management
- [ ] OS integration for space reclamation
- [ ] Redundancy-aware eviction
- [ ] Priority-based fragment retention

### 6.4 Omniscient Omnibox
- [ ] Zero-click RAG answers
- [ ] Query vectorization
- [ ] Local AI synthesis
- [ ] Web search fallback

**Milestone:** Production-ready browser with seamless Web2/Web3 bridge

---

## Phase 7: Security & Governance
**Status:** Design

### 7.1 Verification Layer
- [ ] Manifest hash binding
- [ ] zkTLS domain notarization
- [ ] Institutional Web of Trust
- [ ] AI semantic anomaly detection

### 7.2 Backdoor Resistance
- [ ] Source code on A.E.T.H.E.R. ledger
- [ ] Multi-sig merge requirements
- [ ] Deterministic build system
- [ ] Attestor network for binary verification
- [ ] AI Sentry watchdog process

### 7.3 Phoenix Protocol
- [ ] Guardian Quorum (11 keys, 7-of-11)
- [ ] Priority Zero interrupt signal
- [ ] Scuttle Protocol (isolate, purge, revert, forget)
- [ ] Re-Genesis Attestation Protocol
- [ ] Safe Harbor sealed archives

**Milestone:** Catastrophic-reset capable governance

---

## Phase 8: Economic Model
**Status:** Design

### 8.1 Bandwidth-as-Currency
- [ ] DAG-based micro-ledger
- [ ] Fractional bandwidth credits
- [ ] Credit earning/spending logic
- [ ] Anti-leeching mechanisms

### 8.2 Priority Rewards
- [ ] Redundancy monitoring
- [ ] Low-redundancy bonus credits
- [ ] Vault Node specialization incentives
- [ ] Market dynamics for preservation

**Milestone:** Self-sustaining economic loop

---

## Immediate Next Steps

1. **Performance Benchmarking** - Measure current Python prototype limits
2. **C Core Scaffolding** - Start io_uring/IOCP abstraction layer
3. **Reed-Solomon Library** - Integrate or implement erasure coding
4. **llama.cpp Evaluation** - Test embedded model feasibility
5. **Wasm Runtime Selection** - Evaluate wasmtime vs wasmer

---

## Repository Structure (Future)

```
aether/
├── core/           # C Hyper-Core daemon
├── python/         # Python prototype (current)
├── holographic/    # Reed-Solomon erasure coding
├── semantic/       # AI vectorization & routing
├── sidecar/        # Wasm runtime + CRDTs
├── aegis/          # Browser (Tauri)
├── governance/     # Phoenix Protocol, zkTLS
└── docs/           # Architecture, specs
```

---

## Technology Stack Summary

| Layer | Technology |
|-------|------------|
| Hyper-Core | C, io_uring, eBPF, OpenSSL |
| Holographic | Reed-Solomon (Jerasure/libreedsolomon) |
| Semantic | llama.cpp, ONNX Runtime |
| Sidecar | wasmtime, Rust |
| Aegis | Tauri, Rust, WebView2 |
| Governance | libzk, secp256k1, threshold signatures |

---

*Last Updated: 2026-04-01*
*Version: 0.2.0 (Python Prototype)*
