"""
Error handling and recovery mechanisms for A.E.T.H.E.R.
"""

import time
import threading
import random
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Callable, Any, TypeVar, Generic
from enum import IntEnum, Enum
from collections import defaultdict
import logging
import traceback


logger = logging.getLogger("aether.errors")


class ErrorSeverity(IntEnum):
    """Severity levels for errors."""
    LOW = 1       # Minor issues, can be ignored
    MEDIUM = 2    # Should be handled, but not critical
    HIGH = 3      # Requires immediate attention
    CRITICAL = 4  # System-threatening


class ErrorType(IntEnum):
    """Types of errors."""
    NETWORK = 1
    PROTOCOL = 2
    CRYPTO = 3
    STORAGE = 4
    PEER = 5
    DHT = 6
    CONFIG = 7
    SYSTEM = 8
    TIMEOUT = 9
    RATE_LIMIT = 10


@dataclass
class AetherError:
    """Standardized error representation."""
    code: int
    message: str
    error_type: ErrorType
    severity: ErrorSeverity
    timestamp: float = field(default_factory=time.time)
    component: str = ""
    peer_id: Optional[str] = None
    details: Dict[str, Any] = field(default_factory=dict)
    recoverable: bool = True
    retry_after: Optional[float] = None
    
    def __str__(self) -> str:
        return f"[{self.error_type.name}] {self.message} (code: {self.code})"
    
    def to_dict(self) -> dict:
        return {
            'code': self.code,
            'message': self.message,
            'type': self.error_type.name,
            'severity': self.severity.name,
            'timestamp': self.timestamp,
            'component': self.component,
            'peer_id': self.peer_id,
            'details': self.details,
            'recoverable': self.recoverable,
            'retry_after': self.retry_after
        }


class ErrorCodes:
    """Standard error codes."""
    # Network errors (1000-1999)
    NETWORK_UNREACHABLE = 1001
    CONNECTION_REFUSED = 1002
    CONNECTION_TIMEOUT = 1003
    CONNECTION_RESET = 1004
    DNS_RESOLUTION_FAILED = 1005
    
    # Protocol errors (2000-2999)
    PROTOCOL_VIOLATION = 2001
    INVALID_MESSAGE = 2002
    VERSION_MISMATCH = 2003
    HANDSHAKE_FAILED = 2004
    
    # Crypto errors (3000-3999)
    SIGNATURE_INVALID = 3001
    DECRYPTION_FAILED = 3002
    KEY_GENERATION_FAILED = 3003
    HASH_MISMATCH = 3004
    
    # Storage errors (4000-4999)
    STORAGE_FULL = 4001
    IO_ERROR = 4002
    CORRUPTED_DATA = 4003
    VALUE_NOT_FOUND = 4004
    
    # Peer errors (5000-5999)
    PEER_BLACKLISTED = 5001
    PEER_LIMIT_REACHED = 5002
    PEER_TIMEOUT = 5003
    PEER_MISBEHAVING = 5004
    
    # DHT errors (6000-6999)
    DHT_LOOKUP_FAILED = 6001
    DHT_STORE_FAILED = 6002
    DHT_REPLICATION_FAILED = 6003
    DHT_INCONSISTENT = 6004
    
    # Config errors (7000-7999)
    CONFIG_INVALID = 7001
    CONFIG_MISSING = 7002
    CONFIG_PERMISSION_DENIED = 7003
    
    # System errors (8000-8999)
    OUT_OF_MEMORY = 8001
    OUT_OF_DISK = 8002
    SYSTEM_LIMIT = 8003
    
    # Timeout errors (9000-9999)
    OPERATION_TIMEOUT = 9001
    RESPONSE_TIMEOUT = 9002
    LOCK_TIMEOUT = 9003
    
    # Rate limit errors (10000-10999)
    RATE_LIMIT_EXCEEDED = 10001
    QUOTA_EXCEEDED = 10002


class RetryStrategy(Enum):
    """Retry strategies for recoverable errors."""
    NONE = "none"
    FIXED = "fixed"
    LINEAR = "linear"
    EXPONENTIAL = "exponential"
    EXPONENTIAL_BACKOFF = "exponential_backoff"


@dataclass
class RetryConfig:
    """Configuration for retry behavior."""
    strategy: RetryStrategy = RetryStrategy.EXPONENTIAL_BACKOFF
    max_retries: int = 3
    base_delay: float = 1.0
    max_delay: float = 60.0
    jitter: bool = True


class RetryHandler:
    """Handles retry logic for operations."""
    
    def __init__(self, config: RetryConfig = None):
        self.config = config or RetryConfig()
        
    def get_delay(self, attempt: int) -> float:
        """Calculate delay for a given attempt number."""
        if self.config.strategy == RetryStrategy.NONE:
            return 0
            
        elif self.config.strategy == RetryStrategy.FIXED:
            delay = self.config.base_delay
            
        elif self.config.strategy == RetryStrategy.LINEAR:
            delay = self.config.base_delay * attempt
            
        elif self.config.strategy == RetryStrategy.EXPONENTIAL:
            delay = self.config.base_delay * (2 ** (attempt - 1))
            
        elif self.config.strategy == RetryStrategy.EXPONENTIAL_BACKOFF:
            delay = min(
                self.config.base_delay * (2 ** (attempt - 1)),
                self.config.max_delay
            )
        else:
            delay = 0
            
        # Add jitter to prevent thundering herd
        if self.config.jitter:
            jitter_range = delay * 0.2
            delay += random.uniform(-jitter_range, jitter_range)
            
        return min(delay, self.config.max_delay)
    
    def should_retry(self, attempt: int, error: AetherError) -> bool:
        """Determine if operation should be retried."""
        if not error.recoverable:
            return False
            
        if error.retry_after is not None:
            return time.time() < error.retry_after
            
        return attempt < self.config.max_retries


T = TypeVar('T')


class RetryableOperation(Generic[T]):
    """Decorator for retryable operations."""
    
    def __init__(self, func: Callable[..., T], 
                 retry_handler: RetryHandler,
                 error_handler: Optional[Callable[[AetherError], None]] = None,
                 name: str = ""):
        self.func = func
        self.retry_handler = retry_handler
        self.error_handler = error_handler
        self.name = name or func.__name__
        
    def __call__(self, *args, **kwargs) -> Optional[T]:
        """Execute with retry logic."""
        attempt = 0
        last_error = None
        
        while True:
            try:
                return self.func(*args, **kwargs)
            except AetherException as e:
                last_error = e.error
                attempt += 1
                
                if self.error_handler:
                    self.error_handler(last_error)
                    
                if not self.retry_handler.should_retry(attempt, last_error):
                    break
                    
                delay = self.retry_handler.get_delay(attempt)
                logger.warning(
                    f"{self.name} failed (attempt {attempt}), "
                    f"retrying in {delay:.2f}s: {last_error.message}"
                )
                time.sleep(delay)
                
        logger.error(f"{self.name} failed after {attempt} attempts: {last_error.message}")
        return None


class CircuitBreakerState(Enum):
    """States for circuit breaker."""
    CLOSED = "closed"      # Normal operation
    OPEN = "open"          # Failing, reject requests
    HALF_OPEN = "half_open"  # Testing if service recovered


class CircuitBreaker:
    """
    Circuit breaker pattern implementation.
    Prevents cascading failures by failing fast when a service is unhealthy.
    """
    
    def __init__(self, name: str,
                 failure_threshold: int = 5,
                 success_threshold: int = 2,
                 timeout: float = 30.0):
        self.name = name
        self.failure_threshold = failure_threshold
        self.success_threshold = success_threshold
        self.timeout = timeout
        
        self._state = CircuitBreakerState.CLOSED
        self._failure_count = 0
        self._success_count = 0
        self._last_failure_time: Optional[float] = None
        self._lock = threading.Lock()
        
    @property
    def state(self) -> CircuitBreakerState:
        """Get current state, checking for timeout transition."""
        with self._lock:
            if self._state == CircuitBreakerState.OPEN:
                if (self._last_failure_time and 
                    time.time() - self._last_failure_time >= self.timeout):
                    self._state = CircuitBreakerState.HALF_OPEN
                    self._success_count = 0
            return self._state
            
    def can_execute(self) -> bool:
        """Check if operation can be executed."""
        state = self.state
        if state == CircuitBreakerState.CLOSED:
            return True
        elif state == CircuitBreakerState.OPEN:
            return False
        else:  # HALF_OPEN
            return True
            
    def record_success(self):
        """Record a successful operation."""
        with self._lock:
            if self._state == CircuitBreakerState.HALF_OPEN:
                self._success_count += 1
                if self._success_count >= self.success_threshold:
                    self._state = CircuitBreakerState.CLOSED
                    self._failure_count = 0
                    self._success_count = 0
                    logger.info(f"Circuit breaker '{self.name}' closed")
            elif self._state == CircuitBreakerState.CLOSED:
                self._failure_count = 0
                
    def record_failure(self):
        """Record a failed operation."""
        with self._lock:
            self._failure_count += 1
            self._last_failure_time = time.time()
            
            if self._state == CircuitBreakerState.HALF_OPEN:
                self._state = CircuitBreakerState.OPEN
                logger.warning(f"Circuit breaker '{self.name}' opened (half-open failure)")
            elif self._state == CircuitBreakerState.CLOSED:
                if self._failure_count >= self.failure_threshold:
                    self._state = CircuitBreakerState.OPEN
                    logger.warning(
                        f"Circuit breaker '{self.name}' opened "
                        f"({self._failure_count} failures)"
                    )
                    
    def reset(self):
        """Reset circuit breaker to initial state."""
        with self._lock:
            self._state = CircuitBreakerState.CLOSED
            self._failure_count = 0
            self._success_count = 0
            self._last_failure_time = None


class RecoveryAction(Enum):
    """Types of recovery actions."""
    RETRY = "retry"
    FAILOVER = "failover"
    DEGRADED = "degraded"
    RESTART = "restart"
    ISOLATE = "isolate"
    MANUAL = "manual"


@dataclass
class RecoveryPlan:
    """Plan for recovering from an error."""
    action: RecoveryAction
    priority: int
    description: str
    steps: List[str] = field(default_factory=list)
    estimated_time: float = 0.0
    automatic: bool = True
    requires_restart: bool = False


class RecoveryManager:
    """Manages error recovery strategies."""
    
    def __init__(self):
        self._recovery_plans: Dict[ErrorType, List[RecoveryPlan]] = defaultdict(list)
        self._active_recoveries: Dict[str, float] = {}
        self._lock = threading.Lock()
        self._init_default_plans()
        
    def _init_default_plans(self):
        """Initialize default recovery plans."""
        # Network errors
        self._recovery_plans[ErrorType.NETWORK] = [
            RecoveryPlan(
                action=RecoveryAction.RETRY,
                priority=1,
                description="Retry network operation with backoff",
                steps=[
                    "Wait for backoff period",
                    "Re-resolve DNS if needed",
                    "Attempt reconnection",
                ],
                estimated_time=5.0,
                automatic=True
            ),
            RecoveryPlan(
                action=RecoveryAction.FAILOVER,
                priority=2,
                description="Failover to alternative peer",
                steps=[
                    "Select alternative peer from routing table",
                    "Redirect request",
                    "Update peer preferences",
                ],
                estimated_time=2.0,
                automatic=True
            ),
        ]
        
        # DHT errors
        self._recovery_plans[ErrorType.DHT] = [
            RecoveryPlan(
                action=RecoveryAction.RETRY,
                priority=1,
                description="Retry DHT operation with expanded search",
                steps=[
                    "Increase lookup breadth",
                    "Query additional nodes",
                    "Check local cache",
                ],
                estimated_time=10.0,
                automatic=True
            ),
            RecoveryPlan(
                action=RecoveryAction.DEGRADED,
                priority=2,
                description="Operate in degraded mode",
                steps=[
                    "Use cached/stale data if available",
                    "Mark data as potentially stale",
                    "Schedule background refresh",
                ],
                estimated_time=0.0,
                automatic=True
            ),
        ]
        
        # Peer errors
        self._recovery_plans[ErrorType.PEER] = [
            RecoveryPlan(
                action=RecoveryAction.ISOLATE,
                priority=1,
                description="Isolate misbehaving peer",
                steps=[
                    "Add peer to watchlist",
                    "Reduce peer trust score",
                    "Limit peer requests",
                ],
                estimated_time=0.0,
                automatic=True
            ),
            RecoveryPlan(
                action=RecoveryAction.FAILOVER,
                priority=2,
                description="Find replacement peer",
                steps=[
                    "Query DHT for alternative peers",
                    "Establish new connection",
                    "Sync state if needed",
                ],
                estimated_time=5.0,
                automatic=True
            ),
        ]
        
        # Crypto errors
        self._recovery_plans[ErrorType.CRYPTO] = [
            RecoveryPlan(
                action=RecoveryAction.RESTART,
                priority=1,
                description="Regenerate cryptographic material",
                steps=[
                    "Identify compromised key/material",
                    "Generate new key pair",
                    "Re-sign affected data",
                    "Notify peers of key change",
                ],
                estimated_time=30.0,
                automatic=False,
                requires_restart=True
            ),
        ]
        
        # Storage errors
        self._recovery_plans[ErrorType.STORAGE] = [
            RecoveryPlan(
                action=RecoveryAction.DEGRADED,
                priority=1,
                description="Enter read-only mode",
                steps=[
                    "Disable write operations",
                    "Continue serving reads",
                    "Alert administrator",
                ],
                estimated_time=0.0,
                automatic=True
            ),
            RecoveryPlan(
                action=RecoveryAction.MANUAL,
                priority=2,
                description="Manual intervention required",
                steps=[
                    "Backup current state",
                    "Free disk space or repair storage",
                    "Verify data integrity",
                    "Resume normal operations",
                ],
                estimated_time=300.0,
                automatic=False
            ),
        ]
        
    def get_recovery_plan(self, error: AetherError) -> Optional[RecoveryPlan]:
        """Get the best recovery plan for an error."""
        plans = self._recovery_plans.get(error.error_type, [])
        if not plans:
            return None
            
        # Return highest priority plan
        return max(plans, key=lambda p: p.priority)
    
    def execute_recovery(self, error: AetherError, 
                        context: Optional[Dict[str, Any]] = None) -> bool:
        """
        Attempt to execute recovery for an error.
        Returns True if recovery was successful.
        """
        plan = self.get_recovery_plan(error)
        if not plan:
            logger.warning(f"No recovery plan for error: {error}")
            return False
            
        if not plan.automatic:
            logger.info(f"Recovery plan requires manual intervention: {plan.description}")
            return False
            
        recovery_id = f"{error.error_type.name}_{time.time()}"
        
        with self._lock:
            self._active_recoveries[recovery_id] = time.time()
            
        try:
            logger.info(f"Executing recovery: {plan.description}")
            
            # Execute recovery steps
            for step in plan.steps:
                logger.debug(f"Recovery step: {step}")
                # Actual step execution would be context-dependent
                
            # Simulate recovery time
            if plan.estimated_time > 0:
                time.sleep(min(plan.estimated_time, 1.0))  # Cap at 1s for demo
                
            logger.info(f"Recovery completed: {plan.description}")
            return True
            
        except Exception as e:
            logger.error(f"Recovery failed: {e}")
            return False
        finally:
            with self._lock:
                self._active_recoveries.pop(recovery_id, None)
                
    def get_active_recoveries(self) -> List[str]:
        """Get list of active recovery operations."""
        with self._lock:
            now = time.time()
            # Clean up old recoveries (older than 5 minutes)
            active = [
                rid for rid, start_time in self._active_recoveries.items()
                if now - start_time < 300
            ]
            return active


class AetherException(Exception):
    """Base exception for A.E.T.H.E.R."""
    
    def __init__(self, error: AetherError, cause: Optional[Exception] = None):
        super().__init__(error.message)
        self.error = error
        self.cause = cause
        self.traceback = traceback.format_exc() if cause else None
        
    def __str__(self) -> str:
        if self.cause:
            return f"{self.error.message} (caused by: {self.cause})"
        return self.error.message


class NetworkException(AetherException):
    """Network-related exceptions."""
    pass


class ProtocolException(AetherException):
    """Protocol-related exceptions."""
    pass


class CryptoException(AetherException):
    """Cryptographic exceptions."""
    pass


class StorageException(AetherException):
    """Storage-related exceptions."""
    pass


class PeerException(AetherException):
    """Peer-related exceptions."""
    pass


class DHTException(AetherException):
    """DHT-related exceptions."""
    pass


class ErrorHandler:
    """
    Central error handler for the node.
    Coordinates error logging, recovery, and metrics.
    """
    
    def __init__(self, node_id: str):
        self.node_id = node_id
        self.retry_handler = RetryHandler()
        self.recovery_manager = RecoveryManager()
        self._circuit_breakers: Dict[str, CircuitBreaker] = {}
        self._error_counts: Dict[str, int] = defaultdict(int)
        self._lock = threading.Lock()
        
    def handle_error(self, error: AetherError, 
                    context: Optional[Dict[str, Any]] = None,
                    raise_exception: bool = False) -> bool:
        """
        Handle an error.
        Returns True if error was handled successfully.
        """
        # Log the error
        self._log_error(error)
        
        # Update error counts
        with self._lock:
            key = f"{error.error_type.name}:{error.code}"
            self._error_counts[key] += 1
            
        # Check circuit breaker
        cb_name = f"{error.component}:{error.error_type.name}"
        if self._should_trip_circuit_breaker(cb_name, error):
            return False
            
        # Attempt recovery
        if error.recoverable:
            success = self.recovery_manager.execute_recovery(error, context)
            if success:
                self._record_success(cb_name)
                return True
                
        # Record failure
        self._record_failure(cb_name)
        
        if raise_exception:
            raise self._create_exception(error)
            
        return False
        
    def _log_error(self, error: AetherError):
        """Log error with appropriate level."""
        log_kwargs = {
            'component': error.component,
            'peer_id': error.peer_id,
            'error_code': error.code,
            'details': error.details
        }
        
        if error.severity == ErrorSeverity.CRITICAL:
            logger.critical(f"CRITICAL ERROR: {error}", extra=log_kwargs)
        elif error.severity == ErrorSeverity.HIGH:
            logger.error(f"High severity error: {error}", extra=log_kwargs)
        elif error.severity == ErrorSeverity.MEDIUM:
            logger.warning(f"Medium severity error: {error}", extra=log_kwargs)
        else:
            logger.debug(f"Low severity error: {error}", extra=log_kwargs)
            
    def _should_trip_circuit_breaker(self, name: str, error: AetherError) -> bool:
        """Check if circuit breaker should prevent operation."""
        if name not in self._circuit_breakers:
            self._circuit_breakers[name] = CircuitBreaker(name)
            
        cb = self._circuit_breakers[name]
        if not cb.can_execute():
            logger.warning(f"Circuit breaker '{name}' is open, rejecting request")
            return True
        return False
        
    def _record_success(self, name: str):
        """Record success for circuit breaker."""
        if name in self._circuit_breakers:
            self._circuit_breakers[name].record_success()
            
    def _record_failure(self, name: str):
        """Record failure for circuit breaker."""
        if name in self._circuit_breakers:
            self._circuit_breakers[name].record_failure()
            
    def _create_exception(self, error: AetherError) -> AetherException:
        """Create appropriate exception type for error."""
        exception_map = {
            ErrorType.NETWORK: NetworkException,
            ErrorType.PROTOCOL: ProtocolException,
            ErrorType.CRYPTO: CryptoException,
            ErrorType.STORAGE: StorageException,
            ErrorType.PEER: PeerException,
            ErrorType.DHT: DHTException,
        }
        exception_class = exception_map.get(error.error_type, AetherException)
        return exception_class(error)
        
    def get_error_statistics(self) -> Dict[str, Any]:
        """Get error statistics."""
        with self._lock:
            return {
                'total_errors': sum(self._error_counts.values()),
                'by_type': dict(self._error_counts),
                'circuit_breakers': {
                    name: cb.state.name 
                    for name, cb in self._circuit_breakers.items()
                }
            }
            
    def reset_statistics(self):
        """Reset error statistics."""
        with self._lock:
            self._error_counts.clear()
            
    def retry_operation(self, func: Callable, *args, 
                       error_context: Optional[Dict] = None,
                       **kwargs) -> Optional[Any]:
        """
        Execute an operation with retry logic.
        """
        retryable = RetryableOperation(
            func, 
            self.retry_handler,
            error_handler=lambda err: self.handle_error(err, error_context),
            name=func.__name__
        )
        return retryable(*args, **kwargs)


def create_error(code: int, message: str, error_type: ErrorType,
                severity: ErrorSeverity = ErrorSeverity.MEDIUM,
                component: str = "", peer_id: Optional[str] = None,
                details: Optional[Dict] = None,
                recoverable: bool = True,
                retry_after: Optional[float] = None) -> AetherError:
    """Helper function to create errors."""
    return AetherError(
        code=code,
        message=message,
        error_type=error_type,
        severity=severity,
        component=component,
        peer_id=peer_id,
        details=details or {},
        recoverable=recoverable,
        retry_after=retry_after
    )
