"""
Rate limiting and DoS protection for A.E.T.H.E.R.
Implements multiple rate limiting algorithms and attack detection.
"""

import time
import threading
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple, Callable
from collections import defaultdict
from enum import IntEnum
import logging
import hashlib


logger = logging.getLogger("aether.ratelimit")


class RateLimitAlgorithm(IntEnum):
    """Rate limiting algorithms."""
    TOKEN_BUCKET = 1
    SLIDING_WINDOW = 2
    FIXED_WINDOW = 3
    LEAKY_BUCKET = 4


class ThreatLevel(IntEnum):
    """Threat levels for DoS detection."""
    NONE = 0
    LOW = 1
    MEDIUM = 2
    HIGH = 3
    CRITICAL = 4


@dataclass
class RateLimitConfig:
    """Configuration for rate limiter."""
    algorithm: RateLimitAlgorithm = RateLimitAlgorithm.TOKEN_BUCKET
    requests_per_second: float = 10.0
    burst_size: int = 20
    window_size: float = 60.0  # For sliding/fixed window
    enable_per_peer: bool = True
    enable_global: bool = True
    global_limit: int = 10000


@dataclass
class PeerRateLimit:
    """Rate limit state for a peer."""
    peer_id: bytes
    
    # Token bucket
    tokens: float = 0.0
    last_update: float = field(default_factory=time.time)
    
    # Sliding window
    request_times: List[float] = field(default_factory=list)
    
    # Fixed window
    window_count: int = 0
    window_start: float = field(default_factory=time.time)
    
    # Statistics
    total_requests: int = 0
    rejected_requests: int = 0
    last_request: float = field(default_factory=time.time)


@dataclass
class GlobalRateLimit:
    """Global rate limit state."""
    # Token bucket
    tokens: float = 0.0
    last_update: float = field(default_factory=time.time)
    
    # Request counter
    total_requests: int = 0
    rejected_requests: int = 0


class TokenBucketLimiter:
    """Token bucket rate limiter."""
    
    def __init__(self, rate: float, capacity: int):
        self.rate = rate  # Tokens per second
        self.capacity = capacity
        self._tokens = float(capacity)
        self._last_update = time.time()
        self._lock = threading.Lock()
    
    def allow(self, tokens: int = 1) -> bool:
        """Check if request is allowed."""
        with self._lock:
            now = time.time()
            elapsed = now - self._last_update
            
            # Add tokens based on elapsed time
            self._tokens = min(
                self.capacity,
                self._tokens + elapsed * self.rate
            )
            self._last_update = now
            
            # Check if enough tokens
            if self._tokens >= tokens:
                self._tokens -= tokens
                return True
            return False
    
    def get_tokens(self) -> float:
        """Get current token count."""
        with self._lock:
            now = time.time()
            elapsed = now - self._last_update
            return min(
                self.capacity,
                self._tokens + elapsed * self.rate
            )


class SlidingWindowLimiter:
    """Sliding window rate limiter."""
    
    def __init__(self, limit: int, window_size: float):
        self.limit = limit
        self.window_size = window_size
        self._requests: List[float] = []
        self._lock = threading.Lock()
    
    def allow(self) -> bool:
        """Check if request is allowed."""
        with self._lock:
            now = time.time()
            cutoff = now - self.window_size
            
            # Remove old requests
            self._requests = [t for t in self._requests if t > cutoff]
            
            # Check limit
            if len(self._requests) < self.limit:
                self._requests.append(now)
                return True
            return False
    
    def get_count(self) -> int:
        """Get current request count in window."""
        with self._lock:
            now = time.time()
            cutoff = now - self.window_size
            self._requests = [t for t in self._requests if t > cutoff]
            return len(self._requests)


class FixedWindowLimiter:
    """Fixed window rate limiter."""
    
    def __init__(self, limit: int, window_size: float):
        self.limit = limit
        self.window_size = window_size
        self._count = 0
        self._window_start = time.time()
        self._lock = threading.Lock()
    
    def allow(self) -> bool:
        """Check if request is allowed."""
        with self._lock:
            now = time.time()
            
            # Check if window expired
            if now - self._window_start >= self.window_size:
                self._count = 0
                self._window_start = now
            
            # Check limit
            if self._count < self.limit:
                self._count += 1
                return True
            return False
    
    def get_count(self) -> int:
        """Get current request count in window."""
        with self._lock:
            return self._count


class RateLimiter:
    """
    Main rate limiter combining per-peer and global limits.
    """
    
    def __init__(self, config: RateLimitConfig):
        self.config = config
        self._peer_limits: Dict[bytes, PeerRateLimit] = {}
        self._global_limit: Optional[GlobalRateLimit] = None
        self._lock = threading.RLock()
        
        # Initialize limiters
        if config.enable_global:
            self._global_limit = GlobalRateLimit(
                tokens=config.global_limit
            )
        
        # Create limiter factories based on algorithm
        self._create_limiter = self._get_limiter_factory(config.algorithm)
    
    def _get_limiter_factory(self, algorithm: RateLimitAlgorithm) -> Callable:
        """Get limiter creation function based on algorithm."""
        if algorithm == RateLimitAlgorithm.TOKEN_BUCKET:
            return lambda: TokenBucketLimiter(
                self.config.requests_per_second,
                self.config.burst_size
            )
        elif algorithm == RateLimitAlgorithm.SLIDING_WINDOW:
            return lambda: SlidingWindowLimiter(
                int(self.config.requests_per_second * self.config.window_size),
                self.config.window_size
            )
        elif algorithm == RateLimitAlgorithm.FIXED_WINDOW:
            return lambda: FixedWindowLimiter(
                int(self.config.requests_per_second * self.config.window_size),
                self.config.window_size
            )
        else:
            return lambda: TokenBucketLimiter(
                self.config.requests_per_second,
                self.config.burst_size
            )
    
    def _get_or_create_peer_limit(self, peer_id: bytes) -> PeerRateLimit:
        """Get or create rate limit state for peer."""
        if peer_id not in self._peer_limits:
            self._peer_limits[peer_id] = PeerRateLimit(peer_id=peer_id)
        return self._peer_limits[peer_id]
    
    def allow_request(self, peer_id: bytes, 
                     request_type: str = "default") -> Tuple[bool, float]:
        """
        Check if request is allowed.
        Returns (allowed, retry_after_seconds).
        """
        with self._lock:
            now = time.time()
            
            # Check global limit first
            if self._global_limit and self.config.enable_global:
                if not self._check_global_limit(now):
                    return False, 1.0
            
            # Check per-peer limit
            if self.config.enable_per_peer:
                peer_limit = self._get_or_create_peer_limit(peer_id)
                allowed, retry_after = self._check_peer_limit(
                    peer_limit, now, request_type
                )
                
                # Update statistics
                peer_limit.total_requests += 1
                if not allowed:
                    peer_limit.rejected_requests += 1
                peer_limit.last_request = now
                
                return allowed, retry_after
            
            return True, 0.0
    
    def _check_global_limit(self, now: float) -> bool:
        """Check global rate limit."""
        if not self._global_limit:
            return True
        
        # Token bucket for global limit
        elapsed = now - self._global_limit.last_update
        self._global_limit.tokens = min(
            self.config.global_limit,
            self._global_limit.tokens + elapsed * (self.config.global_limit / 60)
        )
        self._global_limit.last_update = now
        
        if self._global_limit.tokens >= 1:
            self._global_limit.tokens -= 1
            self._global_limit.total_requests += 1
            return True
        
        self._global_limit.rejected_requests += 1
        return False
    
    def _check_peer_limit(self, peer_limit: PeerRateLimit, 
                         now: float, request_type: str) -> Tuple[bool, float]:
        """Check per-peer rate limit."""
        algorithm = self.config.algorithm
        
        if algorithm == RateLimitAlgorithm.TOKEN_BUCKET:
            return self._check_token_bucket(peer_limit, now)
        elif algorithm == RateLimitAlgorithm.SLIDING_WINDOW:
            return self._check_sliding_window(peer_limit, now)
        elif algorithm == RateLimitAlgorithm.FIXED_WINDOW:
            return self._check_fixed_window(peer_limit, now)
        
        return True, 0.0
    
    def _check_token_bucket(self, peer_limit: PeerRateLimit,
                           now: float) -> Tuple[bool, float]:
        """Token bucket check."""
        # Add tokens
        elapsed = now - peer_limit.last_update
        peer_limit.tokens = min(
            self.config.burst_size,
            peer_limit.tokens + elapsed * self.config.requests_per_second
        )
        peer_limit.last_update = now
        
        if peer_limit.tokens >= 1:
            peer_limit.tokens -= 1
            return True, 0.0
        
        # Calculate retry after
        retry_after = (1 - peer_limit.tokens) / self.config.requests_per_second
        return False, retry_after
    
    def _check_sliding_window(self, peer_limit: PeerRateLimit,
                             now: float) -> Tuple[bool, float]:
        """Sliding window check."""
        cutoff = now - self.config.window_size
        
        # Remove old requests
        peer_limit.request_times = [
            t for t in peer_limit.request_times if t > cutoff
        ]
        
        limit = int(self.config.requests_per_second * self.config.window_size)
        
        if len(peer_limit.request_times) < limit:
            peer_limit.request_times.append(now)
            return True, 0.0
        
        # Calculate retry after
        if peer_limit.request_times:
            oldest = min(peer_limit.request_times)
            retry_after = oldest + self.config.window_size - now
        else:
            retry_after = 0.0
        
        return False, max(0.0, retry_after)
    
    def _check_fixed_window(self, peer_limit: PeerRateLimit,
                           now: float) -> Tuple[bool, float]:
        """Fixed window check."""
        # Check if window expired
        if now - peer_limit.window_start >= self.config.window_size:
            peer_limit.window_count = 0
            peer_limit.window_start = now
        
        limit = int(self.config.requests_per_second * self.config.window_size)
        
        if peer_limit.window_count < limit:
            peer_limit.window_count += 1
            return True, 0.0
        
        # Calculate retry after
        retry_after = peer_limit.window_start + self.config.window_size - now
        return False, max(0.0, retry_after)
    
    def get_peer_stats(self, peer_id: bytes) -> dict:
        """Get rate limit stats for peer."""
        with self._lock:
            if peer_id not in self._peer_limits:
                return {}
            
            peer = self._peer_limits[peer_id]
            return {
                'total_requests': peer.total_requests,
                'rejected_requests': peer.rejected_requests,
                'rejection_rate': (
                    peer.rejected_requests / peer.total_requests 
                    if peer.total_requests > 0 else 0
                ),
                'last_request': peer.last_request
            }
    
    def get_global_stats(self) -> dict:
        """Get global rate limit stats."""
        with self._lock:
            if not self._global_limit:
                return {}
            
            return {
                'total_requests': self._global_limit.total_requests,
                'rejected_requests': self._global_limit.rejected_requests,
                'rejection_rate': (
                    self._global_limit.rejected_requests / self._global_limit.total_requests
                    if self._global_limit.total_requests > 0 else 0
                )
            }
    
    def reset_peer(self, peer_id: bytes):
        """Reset rate limit for peer."""
        with self._lock:
            if peer_id in self._peer_limits:
                del self._peer_limits[peer_id]
    
    def reset_all(self):
        """Reset all rate limits."""
        with self._lock:
            self._peer_limits.clear()
            if self._global_limit:
                self._global_limit.tokens = self.config.global_limit
                self._global_limit.last_update = time.time()


@dataclass
class AttackPattern:
    """Detected attack pattern."""
    pattern_type: str
    source_peers: Set[bytes]
    start_time: float
    request_count: int
    threat_level: ThreatLevel
    description: str


class DoSDetector:
    """
    Detects potential DoS attacks based on traffic patterns.
    """
    
    def __init__(self, 
                 threshold_rps: float = 100.0,
                 threshold_peers: int = 100,
                 detection_window: float = 10.0):
        self.threshold_rps = threshold_rps
        self.threshold_peers = threshold_peers
        self.detection_window = detection_window
        
        self._request_history: List[Tuple[float, bytes]] = []
        self._peer_request_counts: Dict[bytes, int] = defaultdict(int)
        self._detected_attacks: List[AttackPattern] = []
        self._lock = threading.Lock()
        
        # Blacklist for immediate rejection
        self._blacklist: Set[bytes] = set()
        self._blacklist_until: Dict[bytes, float] = {}
    
    def record_request(self, peer_id: bytes):
        """Record incoming request for analysis."""
        now = time.time()
        
        with self._lock:
            # Add to history
            self._request_history.append((now, peer_id))
            self._peer_request_counts[peer_id] += 1
            
            # Cleanup old history
            cutoff = now - self.detection_window
            self._request_history = [
                (t, p) for t, p in self._request_history if t > cutoff
            ]
            
            # Analyze for attacks
            self._analyze_patterns(now)
    
    def _analyze_patterns(self, now: float):
        """Analyze traffic patterns for attacks."""
        cutoff = now - self.detection_window
        
        # Count requests in window
        recent_requests = [t for t, _ in self._request_history if t > cutoff]
        rps = len(recent_requests) / self.detection_window
        
        # Count unique peers
        unique_peers = set(p for _, p in self._request_history if p)
        
        # Check for volumetric attack
        if rps > self.threshold_rps:
            threat_level = self._calculate_threat_level(rps, self.threshold_rps)
            
            attack = AttackPattern(
                pattern_type="volumetric",
                source_peers=unique_peers,
                start_time=now,
                request_count=len(recent_requests),
                threat_level=threat_level,
                description=f"High request rate: {rps:.1f} req/s"
            )
            
            self._detected_attacks.append(attack)
            logger.warning(f"DoS attack detected: {attack.description}")
            
            # Auto-blacklist if critical
            if threat_level >= ThreatLevel.CRITICAL:
                self._blacklist_attackers(unique_peers, duration=300)
        
        # Check for distributed attack (many peers)
        if len(unique_peers) > self.threshold_peers:
            threat_level = self._calculate_threat_level(
                len(unique_peers), self.threshold_peers
            )
            
            attack = AttackPattern(
                pattern_type="distributed",
                source_peers=unique_peers,
                start_time=now,
                request_count=len(recent_requests),
                threat_level=threat_level,
                description=f"Many unique peers: {len(unique_peers)}"
            )
            
            self._detected_attacks.append(attack)
            logger.warning(f"Distributed attack detected: {attack.description}")
    
    def _calculate_threat_level(self, value: float, threshold: float) -> ThreatLevel:
        """Calculate threat level based on threshold ratio."""
        ratio = value / threshold
        
        if ratio >= 10:
            return ThreatLevel.CRITICAL
        elif ratio >= 5:
            return ThreatLevel.HIGH
        elif ratio >= 2:
            return ThreatLevel.MEDIUM
        elif ratio >= 1:
            return ThreatLevel.LOW
        return ThreatLevel.NONE
    
    def _blacklist_attackers(self, peer_ids: Set[bytes], duration: float = 300):
        """Blacklist attacking peers."""
        now = time.time()
        
        for peer_id in peer_ids:
            self._blacklist.add(peer_id)
            self._blacklist_until[peer_id] = now + duration
        
        logger.warning(f"Blacklisted {len(peer_ids)} attacking peers")
    
    def is_blacklisted(self, peer_id: bytes) -> bool:
        """Check if peer is blacklisted."""
        now = time.time()
        
        with self._lock:
            # Clean up expired blacklist entries
            expired = [
                pid for pid, until in self._blacklist_until.items()
                if now > until
            ]
            
            for pid in expired:
                self._blacklist.discard(pid)
                self._blacklist_until.pop(pid, None)
            
            return peer_id in self._blacklist
    
    def get_threat_level(self) -> ThreatLevel:
        """Get current threat level."""
        with self._lock:
            if not self._detected_attacks:
                return ThreatLevel.NONE
            
            # Get most recent attack
            latest = self._detected_attacks[-1]
            return latest.threat_level
    
    def get_active_attacks(self) -> List[AttackPattern]:
        """Get currently active attacks."""
        now = time.time()
        cutoff = now - self.detection_window
        
        with self._lock:
            return [
                attack for attack in self._detected_attacks
                if attack.start_time > cutoff
            ]
    
    def clear_blacklist(self):
        """Clear all blacklisted peers."""
        with self._lock:
            self._blacklist.clear()
            self._blacklist_until.clear()
    
    def get_stats(self) -> dict:
        """Get detector statistics."""
        with self._lock:
            return {
                'threat_level': self.get_threat_level().name,
                'active_attacks': len(self.get_active_attacks()),
                'blacklisted_peers': len(self._blacklist),
                'recent_requests': len(self._request_history)
            }


class ProtectionManager:
    """
    Central manager for rate limiting and DoS protection.
    """
    
    def __init__(self, 
                 rate_limit_config: RateLimitConfig = None,
                 dos_threshold_rps: float = 100.0):
        self.rate_limiter = RateLimiter(rate_limit_config or RateLimitConfig())
        self.dos_detector = DoSDetector(threshold_rps=dos_threshold_rps)
        self._lock = threading.Lock()
        
        # Callbacks for blocked requests
        self._block_callbacks: List[Callable] = []
    
    def check_request(self, peer_id: bytes,
                     request_type: str = "default") -> Tuple[bool, str]:
        """
        Check if request should be allowed.
        Returns (allowed, reason).
        """
        # Check blacklist first
        if self.dos_detector.is_blacklisted(peer_id):
            self._trigger_block_callback(peer_id, "blacklisted")
            return False, "Peer is blacklisted"
        
        # Record for DoS detection
        self.dos_detector.record_request(peer_id)
        
        # Check rate limit
        allowed, retry_after = self.rate_limiter.allow_request(
            peer_id, request_type
        )
        
        if not allowed:
            self._trigger_block_callback(peer_id, "rate_limited")
            return False, f"Rate limited, retry after {retry_after:.1f}s"
        
        return True, ""
    
    def on_block(self, callback: Callable[[bytes, str], None]):
        """Register callback for blocked requests."""
        self._block_callbacks.append(callback)
    
    def _trigger_block_callback(self, peer_id: bytes, reason: str):
        """Trigger block callbacks."""
        for callback in self._block_callbacks:
            try:
                callback(peer_id, reason)
            except Exception as e:
                logger.error(f"Block callback error: {e}")
    
    def get_protection_status(self) -> dict:
        """Get comprehensive protection status."""
        return {
            'rate_limiting': {
                'enabled': True,
                'algorithm': self.rate_limiter.config.algorithm.name,
                'global_stats': self.rate_limiter.get_global_stats(),
            },
            'dos_protection': self.dos_detector.get_stats()
        }
    
    def get_peer_protection_info(self, peer_id: bytes) -> dict:
        """Get protection info for specific peer."""
        return {
            'blacklisted': self.dos_detector.is_blacklisted(peer_id),
            'rate_limit_stats': self.rate_limiter.get_peer_stats(peer_id),
            'threat_level': self.dos_detector.get_threat_level().name
        }
    
    def reset_peer(self, peer_id: bytes):
        """Reset all protection state for peer."""
        self.rate_limiter.reset_peer(peer_id)
        logger.info(f"Reset protection state for peer {peer_id.hex()[:8]}...")
    
    def emergency_mode(self, enable: bool):
        """Enable/disable emergency mode (stricter limits)."""
        if enable:
            # Stricter limits
            self.rate_limiter.config.requests_per_second = 1.0
            self.rate_limiter.config.burst_size = 5
            self.dos_detector.threshold_rps = 50.0
            logger.warning("Emergency mode enabled")
        else:
            # Normal limits
            self.rate_limiter.config.requests_per_second = 10.0
            self.rate_limiter.config.burst_size = 20
            self.dos_detector.threshold_rps = 100.0
            logger.info("Emergency mode disabled")
