"""
Comprehensive logging and metrics system for A.E.T.H.E.R.
"""

import time
import json
import logging
import threading
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Callable, Any
from collections import defaultdict
from enum import IntEnum
import os
from datetime import datetime


class LogLevel(IntEnum):
    """Log levels matching standard logging."""
    DEBUG = 10
    INFO = 20
    WARNING = 30
    ERROR = 40
    CRITICAL = 50


class MetricType(IntEnum):
    """Types of metrics."""
    COUNTER = 1      # Monotonically increasing
    GAUGE = 2        # Point-in-time value
    HISTOGRAM = 3    # Distribution of values
    TIMER = 4        # Duration measurement


@dataclass
class MetricValue:
    """A single metric value with metadata."""
    name: str
    value: float
    timestamp: float = field(default_factory=time.time)
    labels: Dict[str, str] = field(default_factory=dict)
    metric_type: MetricType = MetricType.GAUGE
    
    def to_dict(self) -> dict:
        return {
            'name': self.name,
            'value': self.value,
            'timestamp': self.timestamp,
            'labels': self.labels,
            'type': self.metric_type.name
        }


@dataclass
class LogEntry:
    """Structured log entry."""
    timestamp: float
    level: LogLevel
    message: str
    component: str
    node_id: Optional[str] = None
    peer_id: Optional[str] = None
    extra: Dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> dict:
        return {
            'timestamp': self.timestamp,
            'datetime': datetime.fromtimestamp(self.timestamp).isoformat(),
            'level': self.level.name,
            'message': self.message,
            'component': self.component,
            'node_id': self.node_id,
            'peer_id': self.peer_id,
            'extra': self.extra
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict())


class Counter:
    """Thread-safe counter metric."""
    
    def __init__(self, name: str, description: str = "", labels: Optional[Dict[str, str]] = None):
        self.name = name
        self.description = description
        self.labels = labels or {}
        self._value = 0
        self._lock = threading.Lock()
        
    def inc(self, amount: float = 1.0):
        """Increment the counter."""
        with self._lock:
            self._value += amount
            
    def get(self) -> float:
        """Get current value."""
        with self._lock:
            return self._value
            
    def reset(self):
        """Reset counter to zero."""
        with self._lock:
            self._value = 0


class Gauge:
    """Thread-safe gauge metric."""
    
    def __init__(self, name: str, description: str = "", labels: Optional[Dict[str, str]] = None):
        self.name = name
        self.description = description
        self.labels = labels or {}
        self._value = 0.0
        self._lock = threading.Lock()
        
    def set(self, value: float):
        """Set gauge value."""
        with self._lock:
            self._value = value
            
    def inc(self, amount: float = 1.0):
        """Increment gauge."""
        with self._lock:
            self._value += amount
            
    def dec(self, amount: float = 1.0):
        """Decrement gauge."""
        with self._lock:
            self._value -= amount
            
    def get(self) -> float:
        """Get current value."""
        with self._lock:
            return self._value


class Histogram:
    """Thread-safe histogram metric."""
    
    DEFAULT_BUCKETS = (0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0)
    
    def __init__(self, name: str, description: str = "", 
                 labels: Optional[Dict[str, str]] = None,
                 buckets: Optional[tuple] = None):
        self.name = name
        self.description = description
        self.labels = labels or {}
        self.buckets = buckets or self.DEFAULT_BUCKETS
        self._counts = {b: 0 for b in self.buckets}
        self._counts[float('inf')] = 0
        self._sum = 0.0
        self._count = 0
        self._lock = threading.Lock()
        
    def observe(self, value: float):
        """Record a value."""
        with self._lock:
            self._sum += value
            self._count += 1
            for bucket in self.buckets:
                if value <= bucket:
                    self._counts[bucket] += 1
            self._counts[float('inf')] += 1
            
    def get_stats(self) -> dict:
        """Get histogram statistics."""
        with self._lock:
            return {
                'count': self._count,
                'sum': self._sum,
                'mean': self._sum / self._count if self._count > 0 else 0,
                'buckets': dict(self._counts)
            }


class Timer:
    """Context manager for timing operations."""
    
    def __init__(self, histogram: Histogram):
        self.histogram = histogram
        self.start_time: Optional[float] = None
        
    def __enter__(self):
        self.start_time = time.time()
        return self
        
    def __exit__(self, *args):
        if self.start_time is not None:
            duration = time.time() - self.start_time
            self.histogram.observe(duration)


class MetricsRegistry:
    """Central registry for all metrics."""
    
    def __init__(self):
        self._counters: Dict[str, Counter] = {}
        self._gauges: Dict[str, Gauge] = {}
        self._histograms: Dict[str, Histogram] = {}
        self._lock = threading.Lock()
        
    def counter(self, name: str, description: str = "", 
                labels: Optional[Dict[str, str]] = None) -> Counter:
        """Get or create a counter."""
        with self._lock:
            if name not in self._counters:
                self._counters[name] = Counter(name, description, labels)
            return self._counters[name]
            
    def gauge(self, name: str, description: str = "",
              labels: Optional[Dict[str, str]] = None) -> Gauge:
        """Get or create a gauge."""
        with self._lock:
            if name not in self._gauges:
                self._gauges[name] = Gauge(name, description, labels)
            return self._gauges[name]
            
    def histogram(self, name: str, description: str = "",
                  labels: Optional[Dict[str, str]] = None,
                  buckets: Optional[tuple] = None) -> Histogram:
        """Get or create a histogram."""
        with self._lock:
            if name not in self._histograms:
                self._histograms[name] = Histogram(name, description, labels, buckets)
            return self._histograms[name]
            
    def get_all(self) -> List[MetricValue]:
        """Get all current metric values."""
        metrics = []
        timestamp = time.time()
        
        with self._lock:
            for counter in self._counters.values():
                metrics.append(MetricValue(
                    name=counter.name,
                    value=counter.get(),
                    timestamp=timestamp,
                    labels=counter.labels,
                    metric_type=MetricType.COUNTER
                ))
                
            for gauge in self._gauges.values():
                metrics.append(MetricValue(
                    name=gauge.name,
                    value=gauge.get(),
                    timestamp=timestamp,
                    labels=gauge.labels,
                    metric_type=MetricType.GAUGE
                ))
                
            for histogram in self._histograms.values():
                stats = histogram.get_stats()
                metrics.append(MetricValue(
                    name=f"{histogram.name}_count",
                    value=stats['count'],
                    timestamp=timestamp,
                    labels=histogram.labels,
                    metric_type=MetricType.GAUGE
                ))
                metrics.append(MetricValue(
                    name=f"{histogram.name}_sum",
                    value=stats['sum'],
                    timestamp=timestamp,
                    labels=histogram.labels,
                    metric_type=MetricType.GAUGE
                ))
                metrics.append(MetricValue(
                    name=f"{histogram.name}_mean",
                    value=stats['mean'],
                    timestamp=timestamp,
                    labels=histogram.labels,
                    metric_type=MetricType.GAUGE
                ))
                
        return metrics
    
    def to_prometheus(self) -> str:
        """Export metrics in Prometheus format."""
        lines = []
        
        with self._lock:
            for counter in self._counters.values():
                lines.append(f"# TYPE {counter.name} counter")
                lines.append(f"{counter.name} {counter.get()}")
                
            for gauge in self._gauges.values():
                lines.append(f"# TYPE {gauge.name} gauge")
                lines.append(f"{gauge.name} {gauge.get()}")
                
            for histogram in self._histograms.values():
                stats = histogram.get_stats()
                lines.append(f"# TYPE {histogram.name} histogram")
                for bucket, count in stats['buckets'].items():
                    if bucket != float('inf'):
                        lines.append(f'{histogram.name}_bucket{{le="{bucket}"}} {count}')
                lines.append(f'{histogram.name}_bucket{{le="+Inf"}} {stats["count"]}')
                lines.append(f'{histogram.name}_count {stats["count"]}')
                lines.append(f'{histogram.name}_sum {stats["sum"]}')
                
        return "\n".join(lines)


class StructuredLogger:
    """Structured logger with metrics integration."""
    
    def __init__(self, name: str, node_id: Optional[str] = None,
                 log_file: Optional[str] = None,
                 level: LogLevel = LogLevel.INFO):
        self.name = name
        self.node_id = node_id
        self.level = level
        self._handlers: List[logging.Handler] = []
        self._log_buffer: List[LogEntry] = []
        self._buffer_lock = threading.Lock()
        self._max_buffer_size = 10000
        
        # Setup logging
        self._logger = logging.getLogger(name)
        self._logger.setLevel(logging.DEBUG)
        
        # Console handler
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.getLevelName(level))
        console_formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
        )
        console_handler.setFormatter(console_formatter)
        self._logger.addHandler(console_handler)
        self._handlers.append(console_handler)
        
        # File handler if specified
        if log_file:
            os.makedirs(os.path.dirname(log_file) if os.path.dirname(log_file) else '.', exist_ok=True)
            file_handler = logging.FileHandler(log_file)
            file_handler.setLevel(logging.DEBUG)
            file_formatter = logging.Formatter(
                '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
            )
            file_handler.setFormatter(file_formatter)
            self._logger.addHandler(file_handler)
            self._handlers.append(file_handler)
            
    def log(self, level: LogLevel, message: str, 
            component: str = "", peer_id: Optional[str] = None,
            **extra):
        """Log a structured message."""
        if level < self.level:
            return
            
        entry = LogEntry(
            timestamp=time.time(),
            level=level,
            message=message,
            component=component or self.name,
            node_id=self.node_id,
            peer_id=peer_id,
            extra=extra
        )
        
        # Buffer the log entry
        with self._buffer_lock:
            self._log_buffer.append(entry)
            if len(self._log_buffer) > self._max_buffer_size:
                self._log_buffer.pop(0)
                
        # Also log using standard logger
        log_method = self._logger.getLogger().getEffectiveLevel()
        if level == LogLevel.DEBUG:
            self._logger.debug(message, extra=extra)
        elif level == LogLevel.INFO:
            self._logger.info(message, extra=extra)
        elif level == LogLevel.WARNING:
            self._logger.warning(message, extra=extra)
        elif level == LogLevel.ERROR:
            self._logger.error(message, extra=extra)
        elif level == LogLevel.CRITICAL:
            self._logger.critical(message, extra=extra)
            
    def debug(self, message: str, **kwargs):
        self.log(LogLevel.DEBUG, message, **kwargs)
        
    def info(self, message: str, **kwargs):
        self.log(LogLevel.INFO, message, **kwargs)
        
    def warning(self, message: str, **kwargs):
        self.log(LogLevel.WARNING, message, **kwargs)
        
    def error(self, message: str, **kwargs):
        self.log(LogLevel.ERROR, message, **kwargs)
        
    def critical(self, message: str, **kwargs):
        self.log(LogLevel.CRITICAL, message, **kwargs)
        
    def get_recent_logs(self, count: int = 100, 
                        level: Optional[LogLevel] = None) -> List[LogEntry]:
        """Get recent log entries."""
        with self._buffer_lock:
            logs = self._log_buffer[-count:]
            if level:
                logs = [l for l in logs if l.level >= level]
            return logs
            
    def export_logs(self, format: str = "json") -> str:
        """Export logs in specified format."""
        with self._buffer_lock:
            if format == "json":
                return json.dumps([l.to_dict() for l in self._log_buffer])
            elif format == "text":
                return "\n".join([
                    f"{datetime.fromtimestamp(l.timestamp).isoformat()} - {l.level.name} - {l.message}"
                    for l in self._log_buffer
                ])
        return ""


class MetricsCollector:
    """Collects and manages metrics for the node."""
    
    def __init__(self, node_id: str):
        self.node_id = node_id
        self.registry = MetricsRegistry()
        self._collection_thread: Optional[threading.Thread] = None
        self._running = False
        self._collect_interval = 10.0  # seconds
        
        # Initialize standard metrics
        self._init_standard_metrics()
        
    def _init_standard_metrics(self):
        """Initialize standard metrics."""
        # Network metrics
        self.registry.counter('aether_network_bytes_sent_total', 
                             'Total bytes sent over network')
        self.registry.counter('aether_network_bytes_received_total',
                             'Total bytes received over network')
        self.registry.counter('aether_network_messages_sent_total',
                             'Total messages sent')
        self.registry.counter('aether_network_messages_received_total',
                             'Total messages received')
        
        # Peer metrics
        self.registry.gauge('aether_peers_connected',
                           'Number of currently connected peers')
        self.registry.gauge('aether_peers_max',
                           'Maximum number of peers')
        self.registry.counter('aether_peers_total_connections',
                             'Total number of connections made')
        self.registry.counter('aether_peers_total_disconnections',
                             'Total number of disconnections')
        
        # DHT metrics
        self.registry.gauge('aether_dht_keys_stored',
                           'Number of keys stored in DHT')
        self.registry.gauge('aether_dht_nodes_known',
                           'Number of nodes in routing table')
        self.registry.counter('aether_dht_lookups_total',
                             'Total DHT lookups performed')
        self.registry.counter('aether_dht_stores_total',
                             'Total DHT stores performed')
        
        # Performance metrics
        self.registry.histogram('aether_message_latency_seconds',
                               'Message latency distribution')
        self.registry.histogram('aether_lookup_duration_seconds',
                               'DHT lookup duration distribution')
        self.registry.histogram('aether_store_duration_seconds',
                               'DHT store duration distribution')
        
        # Error metrics
        self.registry.counter('aether_errors_total',
                             'Total errors encountered',
                             {'type': 'all'})
        
    def start(self):
        """Start metrics collection."""
        self._running = True
        self._collection_thread = threading.Thread(
            target=self._collection_loop, daemon=True
        )
        self._collection_thread.start()
        
    def stop(self):
        """Stop metrics collection."""
        self._running = False
        if self._collection_thread:
            self._collection_thread.join(timeout=5.0)
            
    def _collection_loop(self):
        """Background metrics collection loop."""
        while self._running:
            time.sleep(self._collect_interval)
            # Additional periodic collection can be added here
            
    def record_message_sent(self, size: int):
        """Record a sent message."""
        self.registry.counter('aether_network_bytes_sent_total').inc(size)
        self.registry.counter('aether_network_messages_sent_total').inc()
        
    def record_message_received(self, size: int):
        """Record a received message."""
        self.registry.counter('aether_network_bytes_received_total').inc(size)
        self.registry.counter('aether_network_messages_received_total').inc()
        
    def record_peer_connected(self):
        """Record a peer connection."""
        self.registry.counter('aether_peers_total_connections').inc()
        
    def record_peer_disconnected(self):
        """Record a peer disconnection."""
        self.registry.counter('aether_peers_total_disconnections').inc()
        
    def record_lookup(self, duration: float):
        """Record a DHT lookup."""
        self.registry.counter('aether_dht_lookups_total').inc()
        self.registry.histogram('aether_lookup_duration_seconds').observe(duration)
        
    def record_store(self, duration: float):
        """Record a DHT store operation."""
        self.registry.counter('aether_dht_stores_total').inc()
        self.registry.histogram('aether_store_duration_seconds').observe(duration)
        
    def record_error(self, error_type: str = "unknown"):
        """Record an error."""
        self.registry.counter('aether_errors_total', labels={'type': error_type}).inc()
        
    def update_peer_count(self, count: int):
        """Update connected peer count."""
        self.registry.gauge('aether_peers_connected').set(count)
        
    def update_dht_stats(self, keys: int, nodes: int):
        """Update DHT statistics."""
        self.registry.gauge('aether_dht_keys_stored').set(keys)
        self.registry.gauge('aether_dht_nodes_known').set(nodes)
        
    def get_metrics(self) -> str:
        """Get all metrics in Prometheus format."""
        # Update dynamic gauges before exporting
        return self.registry.to_prometheus()


@dataclass
class NodeHealth:
    """Health status of a node."""
    status: str  # healthy, degraded, unhealthy
    uptime_seconds: float
    peer_count: int
    dht_keys: int
    dht_nodes: int
    errors_last_minute: int
    last_heartbeat: float
    version: str


class HealthChecker:
    """Monitors node health."""
    
    def __init__(self, node_id: str, version: str = "0.1.0"):
        self.node_id = node_id
        self.version = version
        self.start_time = time.time()
        self._error_times: List[float] = []
        self._lock = threading.Lock()
        
    def record_error(self):
        """Record an error for health calculation."""
        now = time.time()
        with self._lock:
            self._error_times.append(now)
            # Keep only errors from last 5 minutes
            cutoff = now - 300
            self._error_times = [t for t in self._error_times if t > cutoff]
            
    def get_health(self, peer_count: int, dht_keys: int, 
                   dht_nodes: int) -> NodeHealth:
        """Get current health status."""
        now = time.time()
        uptime = now - self.start_time
        
        with self._lock:
            # Count errors in last minute
            cutoff = now - 60
            errors_last_minute = sum(1 for t in self._error_times if t > cutoff)
            
        # Determine health status
        if errors_last_minute > 10:
            status = "unhealthy"
        elif errors_last_minute > 3 or peer_count < 3:
            status = "degraded"
        else:
            status = "healthy"
            
        return NodeHealth(
            status=status,
            uptime_seconds=uptime,
            peer_count=peer_count,
            dht_keys=dht_keys,
            dht_nodes=dht_nodes,
            errors_last_minute=errors_last_minute,
            last_heartbeat=now,
            version=self.version
        )
