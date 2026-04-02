"""
Advanced peer reputation and trust system for A.E.T.H.E.R.
Implements peer scoring, trust calculation, and misbehavior detection.
"""

import time
import threading
import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple
from enum import IntEnum
from collections import defaultdict
import logging


logger = logging.getLogger("aether.trust")


class TrustLevel(IntEnum):
    """Trust levels for peers."""
    BLACKLISTED = 0    # Known bad actor
    DISTRUSTED = 1     # Low trust, limited interaction
    NEUTRAL = 2        # Default for new peers
    TRUSTED = 3        # Good standing
    VERIFIED = 4       # Long-term trusted peer


class MisbehaviorType(IntEnum):
    """Types of peer misbehavior."""
    TIMEOUT = 1           # Failed to respond in time
    INVALID_MESSAGE = 2   # Sent malformed messages
    WRONG_DATA = 3        # Provided incorrect data
    SPAM = 4             # Excessive requests
    PROTOCOL_VIOLATION = 5  # Broke protocol rules
    CONNECTION_ABUSE = 6    # Connection manipulation


@dataclass
class TrustScore:
    """Comprehensive trust score for a peer."""
    peer_id: bytes
    
    # Base scores (0-100)
    reliability_score: float = 50.0  # How reliable is the peer
    honesty_score: float = 50.0      # How honest/truthful
    responsiveness_score: float = 50.0  # How responsive
    longevity_score: float = 0.0     # Based on time known
    
    # Derived metrics
    overall_trust: float = 50.0
    trust_level: TrustLevel = TrustLevel.NEUTRAL
    
    # History
    total_interactions: int = 0
    successful_interactions: int = 0
    failed_interactions: int = 0
    
    # Timestamps
    first_seen: float = field(default_factory=time.time)
    last_interaction: float = field(default_factory=time.time)
    last_score_update: float = field(default_factory=time.time)
    
    def update_overall_trust(self):
        """Calculate overall trust score."""
        # Weighted average of component scores
        self.overall_trust = (
            self.reliability_score * 0.35 +
            self.honesty_score * 0.35 +
            self.responsiveness_score * 0.20 +
            self.longevity_score * 0.10
        )
        
        # Update trust level based on overall score
        if self.overall_trust >= 80:
            self.trust_level = TrustLevel.VERIFIED
        elif self.overall_trust >= 60:
            self.trust_level = TrustLevel.TRUSTED
        elif self.overall_trust >= 40:
            self.trust_level = TrustLevel.NEUTRAL
        elif self.overall_trust >= 20:
            self.trust_level = TrustLevel.DISTRUSTED
        else:
            self.trust_level = TrustLevel.BLACKLISTED
            
    def success_rate(self) -> float:
        """Calculate success rate of interactions."""
        if self.total_interactions == 0:
            return 0.0
        return self.successful_interactions / self.total_interactions
        
    def to_dict(self) -> dict:
        return {
            'peer_id': self.peer_id.hex()[:16] + '...',
            'overall_trust': round(self.overall_trust, 2),
            'trust_level': self.trust_level.name,
            'reliability': round(self.reliability_score, 2),
            'honesty': round(self.honesty_score, 2),
            'responsiveness': round(self.responsiveness_score, 2),
            'longevity': round(self.longevity_score, 2),
            'success_rate': round(self.success_rate() * 100, 2),
            'total_interactions': self.total_interactions
        }


@dataclass
class MisbehaviorRecord:
    """Record of a peer misbehavior incident."""
    timestamp: float
    misbehavior_type: MisbehaviorType
    severity: int  # 1-10
    description: str
    evidence: Optional[bytes] = None


@dataclass
class PeerStatistics:
    """Statistical data about peer behavior."""
    response_times: List[float] = field(default_factory=list)
    request_counts: Dict[str, int] = field(default_factory=lambda: defaultdict(int))
    error_counts: Dict[str, int] = field(default_factory=lambda: defaultdict(int))
    uptime_samples: List[Tuple[float, bool]] = field(default_factory=list)
    
    def avg_response_time(self) -> float:
        """Calculate average response time."""
        if not self.response_times:
            return 0.0
        return sum(self.response_times) / len(self.response_times)
        
    def response_time_p95(self) -> float:
        """Calculate 95th percentile response time."""
        if not self.response_times:
            return 0.0
        sorted_times = sorted(self.response_times)
        idx = int(len(sorted_times) * 0.95)
        return sorted_times[min(idx, len(sorted_times) - 1)]
        
    def add_response_time(self, duration: float, max_samples: int = 1000):
        """Add a response time sample."""
        self.response_times.append(duration)
        if len(self.response_times) > max_samples:
            self.response_times.pop(0)


class PeerReputation:
    """
    Manages peer reputation scoring.
    Uses exponential moving average for score updates.
    """
    
    def __init__(self, alpha: float = 0.1):
        self.alpha = alpha  # Learning rate for score updates
        self._lock = threading.Lock()
        
    def update_reliability(self, score: TrustScore, success: bool):
        """Update reliability score based on interaction result."""
        target = 100.0 if success else 0.0
        score.reliability_score = (
            (1 - self.alpha) * score.reliability_score + 
            self.alpha * target
        )
        score.reliability_score = max(0, min(100, score.reliability_score))
        
    def update_honesty(self, score: TrustScore, verified_honest: bool):
        """Update honesty score based on data verification."""
        target = 100.0 if verified_honest else 0.0
        score.honesty_score = (
            (1 - self.alpha * 2) * score.honesty_score +  # Faster decay for dishonesty
            self.alpha * 2 * target
        )
        score.honesty_score = max(0, min(100, score.honesty_score))
        
    def update_responsiveness(self, score: TrustScore, response_time: float,
                             expected_time: float = 5.0):
        """Update responsiveness score based on response time."""
        # Score based on how fast compared to expected
        if response_time <= 0:
            target = 0
        elif response_time <= expected_time:
            target = 100.0 * (1 - response_time / expected_time / 2)
        else:
            target = max(0, 50.0 * (expected_time / response_time))
            
        score.responsiveness_score = (
            (1 - self.alpha) * score.responsiveness_score +
            self.alpha * target
        )
        score.responsiveness_score = max(0, min(100, score.responsiveness_score))
        
    def update_longevity(self, score: TrustScore):
        """Update longevity score based on time known."""
        days_known = (time.time() - score.first_seen) / 86400
        # Cap at 100 after 30 days
        score.longevity_score = min(100, days_known * 3.33)
        
    def record_interaction(self, score: TrustScore, success: bool,
                          response_time: Optional[float] = None):
        """Record a complete interaction and update all scores."""
        score.total_interactions += 1
        if success:
            score.successful_interactions += 1
        else:
            score.failed_interactions += 1
            
        score.last_interaction = time.time()
        
        # Update component scores
        self.update_reliability(score, success)
        
        if response_time is not None:
            self.update_responsiveness(score, response_time)
            
        self.update_longevity(score)
        score.update_overall_trust()


class MisbehaviorTracker:
    """Tracks and penalizes peer misbehavior."""
    
    def __init__(self, decay_hours: float = 24.0):
        self.decay_hours = decay_hours
        self._misbehaviors: Dict[bytes, List[MisbehaviorRecord]] = defaultdict(list)
        self._lock = threading.Lock()
        
    def record_misbehavior(self, peer_id: bytes, 
                          misbehavior_type: MisbehaviorType,
                          severity: int = 5,
                          description: str = "",
                          evidence: Optional[bytes] = None):
        """Record a misbehavior incident."""
        record = MisbehaviorRecord(
            timestamp=time.time(),
            misbehavior_type=misbehavior_type,
            severity=max(1, min(10, severity)),
            description=description,
            evidence=evidence
        )
        
        with self._lock:
            self._misbehaviors[peer_id].append(record)
            # Keep only last 100 incidents
            if len(self._misbehaviors[peer_id]) > 100:
                self._misbehaviors[peer_id].pop(0)
                
        logger.warning(
            f"Peer {peer_id.hex()[:8]}... misbehavior: "
            f"{misbehavior_type.name} (severity {severity})"
        )
        
    def get_penalty(self, peer_id: bytes) -> float:
        """
        Calculate trust penalty based on recent misbehaviors.
        Returns value 0-100 to subtract from trust score.
        """
        now = time.time()
        decay_seconds = self.decay_hours * 3600
        
        with self._lock:
            records = self._misbehaviors.get(peer_id, [])
            
            if not records:
                return 0.0
                
            penalty = 0.0
            for record in records:
                # Exponential decay based on time
                age_seconds = now - record.timestamp
                if age_seconds > decay_seconds * 7:  # Ignore after 1 week
                    continue
                    
                decay_factor = math.exp(-age_seconds / decay_seconds)
                penalty += record.severity * 10 * decay_factor
                
        return min(100, penalty)
        
    def get_misbehaviors(self, peer_id: bytes, 
                        hours: float = 24) -> List[MisbehaviorRecord]:
        """Get recent misbehaviors for a peer."""
        cutoff = time.time() - (hours * 3600)
        
        with self._lock:
            records = self._misbehaviors.get(peer_id, [])
            return [r for r in records if r.timestamp > cutoff]
            
    def clear_misbehaviors(self, peer_id: bytes):
        """Clear all misbehaviors for a peer (e.g., after appeal)."""
        with self._lock:
            self._misbehaviors.pop(peer_id, [])
            
    def get_repeat_offenders(self, min_incidents: int = 5,
                            hours: float = 24) -> List[bytes]:
        """Get peers with repeated misbehaviors."""
        cutoff = time.time() - (hours * 3600)
        offenders = []
        
        with self._lock:
            for peer_id, records in self._misbehaviors.items():
                recent = [r for r in records if r.timestamp > cutoff]
                if len(recent) >= min_incidents:
                    offenders.append(peer_id)
                    
        return offenders


class TrustManager:
    """
    Central manager for peer trust and reputation.
    Combines reputation scoring with misbehavior tracking.
    """
    
    def __init__(self):
        self._scores: Dict[bytes, TrustScore] = {}
        self._statistics: Dict[bytes, PeerStatistics] = {}
        self.reputation = PeerReputation(alpha=0.1)
        self.misbehavior_tracker = MisbehaviorTracker()
        self._blacklist: Set[bytes] = set()
        self._whitelist: Set[bytes] = set()
        self._lock = threading.RLock()
        
    def get_or_create_score(self, peer_id: bytes) -> TrustScore:
        """Get or create trust score for a peer."""
        with self._lock:
            if peer_id not in self._scores:
                self._scores[peer_id] = TrustScore(peer_id=peer_id)
            return self._scores[peer_id]
            
    def get_or_create_stats(self, peer_id: bytes) -> PeerStatistics:
        """Get or create statistics for a peer."""
        with self._lock:
            if peer_id not in self._statistics:
                self._statistics[peer_id] = PeerStatistics()
            return self._statistics[peer_id]
            
    def record_successful_interaction(self, peer_id: bytes,
                                    response_time: float):
        """Record a successful interaction with a peer."""
        with self._lock:
            score = self.get_or_create_score(peer_id)
            stats = self.get_or_create_stats(peer_id)
            
            stats.add_response_time(response_time)
            self.reputation.record_interaction(
                score, success=True, response_time=response_time
            )
            
    def record_failed_interaction(self, peer_id: bytes):
        """Record a failed interaction with a peer."""
        with self._lock:
            score = self.get_or_create_score(peer_id)
            self.reputation.record_interaction(score, success=False)
            
            # Record misbehavior
            self.misbehavior_tracker.record_misbehavior(
                peer_id,
                MisbehaviorType.TIMEOUT,
                severity=3,
                description="Failed to respond"
            )
            
    def record_data_verification(self, peer_id: bytes, 
                                data_valid: bool):
        """Record result of data verification from peer."""
        with self._lock:
            score = self.get_or_create_score(peer_id)
            
            if data_valid:
                self.reputation.update_honesty(score, verified_honest=True)
            else:
                self.reputation.update_honesty(score, verified_honest=False)
                self.misbehavior_tracker.record_misbehavior(
                    peer_id,
                    MisbehaviorType.WRONG_DATA,
                    severity=7,
                    description="Provided invalid data"
                )
                
    def record_invalid_message(self, peer_id: bytes, 
                              description: str = ""):
        """Record receipt of invalid message from peer."""
        self.misbehavior_tracker.record_misbehavior(
            peer_id,
            MisbehaviorType.INVALID_MESSAGE,
            severity=4,
            description=description or "Invalid message format"
        )
        
    def record_spam(self, peer_id: bytes, 
                   request_count: int):
        """Record spam behavior from peer."""
        severity = min(10, max(1, request_count // 100))
        self.misbehavior_tracker.record_misbehavior(
            peer_id,
            MisbehaviorType.SPAM,
            severity=severity,
            description=f"Excessive requests: {request_count}"
        )
        
    def get_trust_level(self, peer_id: bytes) -> TrustLevel:
        """Get trust level for a peer."""
        with self._lock:
            if peer_id in self._blacklist:
                return TrustLevel.BLACKLISTED
            if peer_id in self._whitelist:
                return TrustLevel.VERIFIED
                
            score = self.get_or_create_score(peer_id)
            
            # Apply misbehavior penalty
            penalty = self.misbehavior_tracker.get_penalty(peer_id)
            adjusted_trust = max(0, score.overall_trust - penalty)
            
            # Determine trust level from adjusted trust
            if adjusted_trust >= 80:
                return TrustLevel.VERIFIED
            elif adjusted_trust >= 60:
                return TrustLevel.TRUSTED
            elif adjusted_trust >= 40:
                return TrustLevel.NEUTRAL
            elif adjusted_trust >= 20:
                return TrustLevel.DISTRUSTED
            else:
                return TrustLevel.BLACKLISTED
                
    def get_trust_score(self, peer_id: bytes) -> float:
        """Get adjusted trust score for a peer."""
        with self._lock:
            if peer_id in self._blacklist:
                return 0.0
            if peer_id in self._whitelist:
                return 100.0
                
            score = self.get_or_create_score(peer_id)
            penalty = self.misbehavior_tracker.get_penalty(peer_id)
            return max(0, score.overall_trust - penalty)
            
    def is_peer_allowed(self, peer_id: bytes) -> bool:
        """Check if peer is allowed to interact."""
        trust_level = self.get_trust_level(peer_id)
        return trust_level >= TrustLevel.NEUTRAL
        
    def blacklist_peer(self, peer_id: bytes, reason: str = ""):
        """Manually blacklist a peer."""
        with self._lock:
            self._blacklist.add(peer_id)
            self._whitelist.discard(peer_id)
            
            # Record severe misbehavior
            self.misbehavior_tracker.record_misbehavior(
                peer_id,
                MisbehaviorType.PROTOCOL_VIOLATION,
                severity=10,
                description=f"Blacklisted: {reason}"
            )
            
        logger.warning(f"Peer {peer_id.hex()[:8]}... blacklisted: {reason}")
        
    def whitelist_peer(self, peer_id: bytes, reason: str = ""):
        """Manually whitelist a peer."""
        with self._lock:
            self._whitelist.add(peer_id)
            self._blacklist.discard(peer_id)
            
        logger.info(f"Peer {peer_id.hex()[:8]}... whitelisted: {reason}")
        
    def remove_peer(self, peer_id: bytes):
        """Remove all data for a peer."""
        with self._lock:
            self._scores.pop(peer_id, None)
            self._statistics.pop(peer_id, None)
            self._blacklist.discard(peer_id)
            self._whitelist.discard(peer_id)
            
    def get_trusted_peers(self, min_trust: TrustLevel = TrustLevel.TRUSTED,
                         limit: int = 100) -> List[bytes]:
        """Get list of trusted peers."""
        with self._lock:
            trusted = []
            for peer_id, score in self._scores.items():
                if self.get_trust_level(peer_id) >= min_trust:
                    trusted.append(peer_id)
                    
            # Sort by trust score descending
            trusted.sort(
                key=lambda pid: self.get_trust_score(pid),
                reverse=True
            )
            
            return trusted[:limit]
            
    def get_statistics_summary(self) -> dict:
        """Get summary statistics about trust system."""
        with self._lock:
            if not self._scores:
                return {
                    'total_peers': 0,
                    'avg_trust_score': 0,
                    'by_trust_level': {},
                    'blacklisted_count': len(self._blacklist),
                    'whitelisted_count': len(self._whitelist)
                }
                
            scores = [s.overall_trust for s in self._scores.values()]
            
            by_level = defaultdict(int)
            for peer_id in self._scores.keys():
                level = self.get_trust_level(peer_id).name
                by_level[level] += 1
                
            return {
                'total_peers': len(self._scores),
                'avg_trust_score': round(sum(scores) / len(scores), 2),
                'by_trust_level': dict(by_level),
                'blacklisted_count': len(self._blacklist),
                'whitelisted_count': len(self._whitelist)
            }
            
    def export_peer_report(self, peer_id: bytes) -> dict:
        """Export detailed report for a peer."""
        with self._lock:
            score = self.get_or_create_score(peer_id)
            stats = self.get_or_create_stats(peer_id)
            misbehaviors = self.misbehavior_tracker.get_misbehaviors(peer_id)
            
            return {
                'trust': score.to_dict(),
                'statistics': {
                    'avg_response_time_ms': round(stats.avg_response_time() * 1000, 2),
                    'p95_response_time_ms': round(stats.response_time_p95() * 1000, 2),
                    'request_counts': dict(stats.request_counts),
                    'error_counts': dict(stats.error_counts)
                },
                'recent_misbehaviors': [
                    {
                        'type': m.misbehavior_type.name,
                        'severity': m.severity,
                        'description': m.description,
                        'timestamp': m.timestamp
                    }
                    for m in misbehaviors[-10:]  # Last 10
                ],
                'blacklisted': peer_id in self._blacklist,
                'whitelisted': peer_id in self._whitelist
            }
