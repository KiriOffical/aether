"""
Benchmark suite for A.E.T.H.E.R. P2P network.
Measures performance, throughput, and latency.
"""

import time
import os
import sys
import statistics
import threading
import random
import json
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, field
from concurrent.futures import ThreadPoolExecutor, as_completed
import tempfile
import shutil

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether import Node, Config, Client
from aether.crypto import Crypto


@dataclass
class BenchmarkResult:
    """Results from a benchmark run."""
    name: str
    operations: int
    duration_seconds: float
    errors: int = 0
    
    @property
    def ops_per_second(self) -> float:
        if self.duration_seconds == 0:
            return 0
        return self.operations / self.duration_seconds
        
    @property
    def ms_per_op(self) -> float:
        if self.operations == 0:
            return 0
        return (self.duration_seconds * 1000) / self.operations
        
    def to_dict(self) -> dict:
        return {
            'name': self.name,
            'operations': self.operations,
            'duration_seconds': self.duration_seconds,
            'errors': self.errors,
            'ops_per_second': round(self.ops_per_second, 2),
            'ms_per_op': round(self.ms_per_op, 3)
        }


@dataclass
class LatencyResult:
    """Latency benchmark results."""
    name: str
    samples: List[float] = field(default_factory=list)
    errors: int = 0
    
    @property
    def count(self) -> int:
        return len(self.samples)
        
    @property
    def mean(self) -> float:
        return statistics.mean(self.samples) if self.samples else 0
        
    @property
    def median(self) -> float:
        return statistics.median(self.samples) if self.samples else 0
        
    @property
    def p95(self) -> float:
        if not self.samples:
            return 0
        sorted_samples = sorted(self.samples)
        idx = int(len(sorted_samples) * 0.95)
        return sorted_samples[min(idx, len(sorted_samples) - 1)]
        
    @property
    def p99(self) -> float:
        if not self.samples:
            return 0
        sorted_samples = sorted(self.samples)
        idx = int(len(sorted_samples) * 0.99)
        return sorted_samples[min(idx, len(sorted_samples) - 1)]
        
    @property
    def min_latency(self) -> float:
        return min(self.samples) if self.samples else 0
        
    @property
    def max_latency(self) -> float:
        return max(self.samples) if self.samples else 0
        
    @property
    def stddev(self) -> float:
        return statistics.stdev(self.samples) if len(self.samples) > 1 else 0
        
    def to_dict(self) -> dict:
        return {
            'name': self.name,
            'count': self.count,
            'mean_ms': round(self.mean, 3),
            'median_ms': round(self.median, 3),
            'p95_ms': round(self.p95, 3),
            'p99_ms': round(self.p99, 3),
            'min_ms': round(self.min_latency, 3),
            'max_ms': round(self.max_latency, 3),
            'stddev_ms': round(self.stddev, 3),
            'errors': self.errors
        }


class BenchmarkSuite:
    """Suite of benchmarks for A.E.T.H.E.R."""
    
    def __init__(self, temp_dir: Optional[str] = None):
        self.temp_dir = temp_dir or tempfile.mkdtemp(prefix="aether_bench_")
        self.results: Dict[str, BenchmarkResult] = {}
        self.latency_results: Dict[str, LatencyResult] = {}
        self.nodes: List[Node] = []
        
    def cleanup(self):
        """Cleanup benchmark resources."""
        for node in self.nodes:
            try:
                node.stop()
            except:
                pass
                
        if os.path.exists(self.temp_dir):
            shutil.rmtree(self.temp_dir)
            
    def create_node(self, port: int = 17821, name: str = "bench_node") -> Node:
        """Create a benchmark node."""
        node_dir = os.path.join(self.temp_dir, name)
        os.makedirs(node_dir, exist_ok=True)
        
        config = Config(
            identity_path=os.path.join(node_dir, "identity.bin"),
            data_dir=node_dir,
            listen_port=port,
            listen_addr="127.0.0.1",
            max_connections=1000,
            bootstrap_nodes=[],
            log_level=50,  # ERROR only
        )
        
        node = Node(config)
        node.start()
        self.nodes.append(node)
        time.sleep(0.2)  # Allow node to start
        
        return node
        
    def benchmark_dht_store(self, node: Node, num_operations: int = 1000,
                           key_size: int = 32, value_size: int = 100) -> BenchmarkResult:
        """Benchmark DHT store operations."""
        errors = 0
        start_time = time.time()
        
        for i in range(num_operations):
            try:
                key = os.urandom(key_size)
                value = os.urandom(value_size)
                success = node.dht_store(key, value)
                if not success:
                    errors += 1
            except Exception:
                errors += 1
                
        duration = time.time() - start_time
        
        result = BenchmarkResult(
            name=f"dht_store_{num_operations}_ops",
            operations=num_operations,
            duration_seconds=duration,
            errors=errors
        )
        
        self.results['dht_store'] = result
        return result
        
    def benchmark_dht_get(self, node: Node, num_operations: int = 1000,
                         key_size: int = 32) -> BenchmarkResult:
        """Benchmark DHT get operations."""
        # First, store some values
        keys = []
        for i in range(num_operations):
            key = os.urandom(key_size)
            value = os.urandom(100)
            node.dht_store(key, value)
            keys.append(key)
            
        # Now benchmark gets
        errors = 0
        start_time = time.time()
        
        for key in keys:
            try:
                value = node.dht_get(key)
                if value is None:
                    errors += 1
            except Exception:
                errors += 1
                
        duration = time.time() - start_time
        
        result = BenchmarkResult(
            name=f"dht_get_{num_operations}_ops",
            operations=num_operations,
            duration_seconds=duration,
            errors=errors
        )
        
        self.results['dht_get'] = result
        return result
        
    def benchmark_dht_latency(self, node: Node, num_samples: int = 100) -> LatencyResult:
        """Benchmark DHT operation latency."""
        latencies = []
        errors = 0
        
        for i in range(num_samples):
            try:
                key = os.urandom(32)
                value = os.urandom(100)
                
                # Measure store latency
                start = time.time()
                node.dht_store(key, value)
                store_latency = (time.time() - start) * 1000  # ms
                
                # Measure get latency
                start = time.time()
                node.dht_get(key)
                get_latency = (time.time() - start) * 1000  # ms
                
                latencies.extend([store_latency, get_latency])
            except Exception:
                errors += 1
                
        result = LatencyResult(
            name="dht_latency",
            samples=latencies,
            errors=errors
        )
        
        self.latency_results['dht_latency'] = result
        return result
        
    def benchmark_concurrent_stores(self, node: Node, num_threads: int = 10,
                                   ops_per_thread: int = 100) -> BenchmarkResult:
        """Benchmark concurrent store operations."""
        errors = 0
        total_ops = num_threads * ops_per_thread
        
        def worker(thread_id: int):
            nonlocal errors
            thread_errors = 0
            
            for i in range(ops_per_thread):
                try:
                    key = f"thread_{thread_id}_key_{i}".encode()
                    value = f"thread_{thread_id}_value_{i}".encode()
                    node.dht_store(key, value)
                except Exception:
                    thread_errors += 1
                    
            return thread_errors
            
        start_time = time.time()
        
        with ThreadPoolExecutor(max_workers=num_threads) as executor:
            futures = [executor.submit(worker, i) for i in range(num_threads)]
            for future in as_completed(futures):
                errors += future.result()
                
        duration = time.time() - start_time
        
        result = BenchmarkResult(
            name=f"concurrent_stores_{num_threads}_threads",
            operations=total_ops,
            duration_seconds=duration,
            errors=errors
        )
        
        self.results['concurrent_stores'] = result
        return result
        
    def benchmark_crypto_operations(self, num_operations: int = 1000) -> BenchmarkResult:
        """Benchmark cryptographic operations."""
        errors = 0
        start_time = time.time()
        
        for i in range(num_operations):
            try:
                # Key generation
                keypair = KeyPair.generate()
                
                # Sign
                data = os.urandom(100)
                signature = keypair.sign(data)
                
                # Verify
                keypair.verify(data, signature)
            except Exception:
                errors += 1
                
        duration = time.time() - start_time
        
        result = BenchmarkResult(
            name=f"crypto_{num_operations}_ops",
            operations=num_operations,
            duration_seconds=duration,
            errors=errors
        )
        
        self.results['crypto'] = result
        return result
        
    def benchmark_hash_operations(self, num_operations: int = 10000) -> BenchmarkResult:
        """Benchmark hashing operations."""
        errors = 0
        start_time = time.time()
        
        for i in range(num_operations):
            try:
                data = os.urandom(100)
                hash_value = Crypto.hash(data)
            except Exception:
                errors += 1
                
        duration = time.time() - start_time
        
        result = BenchmarkResult(
            name=f"hash_{num_operations}_ops",
            operations=num_operations,
            duration_seconds=duration,
            errors=errors
        )
        
        self.results['hash'] = result
        return result
        
    def benchmark_node_id_generation(self, num_operations: int = 1000) -> BenchmarkResult:
        """Benchmark node ID generation."""
        errors = 0
        start_time = time.time()
        
        for i in range(num_operations):
            try:
                keypair = KeyPair.generate()
                node_id = Crypto.node_id(keypair.public_key)
            except Exception:
                errors += 1
                
        duration = time.time() - start_time
        
        result = BenchmarkResult(
            name=f"node_id_{num_operations}_ops",
            operations=num_operations,
            duration_seconds=duration,
            errors=errors
        )
        
        self.results['node_id'] = result
        return result
        
    def run_all_benchmarks(self) -> Dict[str, BenchmarkResult]:
        """Run all benchmarks."""
        print("Starting A.E.T.H.E.R. Benchmark Suite")
        print("=" * 60)
        
        # Create benchmark node
        print("\n[1/8] Starting benchmark node...")
        node = self.create_node(port=17821)
        print(f"      Node ID: {node.node_id.hex()[:16]}...")
        
        # Run benchmarks
        print("\n[2/8] Benchmarking DHT store operations...")
        store_result = self.benchmark_dht_store(node, num_operations=1000)
        print(f"      {store_result.operations} ops in {store_result.duration_seconds:.2f}s")
        print(f"      {store_result.ops_per_second:.2f} ops/sec")
        
        print("\n[3/8] Benchmarking DHT get operations...")
        get_result = self.benchmark_dht_get(node, num_operations=1000)
        print(f"      {get_result.operations} ops in {get_result.duration_seconds:.2f}s")
        print(f"      {get_result.ops_per_second:.2f} ops/sec")
        
        print("\n[4/8] Benchmarking DHT latency...")
        latency_result = self.benchmark_dht_latency(node, num_samples=100)
        print(f"      Mean: {latency_result.mean:.3f}ms")
        print(f"      P95: {latency_result.p95:.3f}ms")
        print(f"      P99: {latency_result.p99:.3f}ms")
        
        print("\n[5/8] Benchmarking concurrent stores...")
        concurrent_result = self.benchmark_concurrent_stores(node, num_threads=10, ops_per_thread=100)
        print(f"      {concurrent_result.operations} ops in {concurrent_result.duration_seconds:.2f}s")
        print(f"      {concurrent_result.ops_per_second:.2f} ops/sec")
        
        print("\n[6/8] Benchmarking crypto operations...")
        crypto_result = self.benchmark_crypto_operations(num_operations=1000)
        print(f"      {crypto_result.operations} ops in {crypto_result.duration_seconds:.2f}s")
        print(f"      {crypto_result.ops_per_second:.2f} ops/sec")
        
        print("\n[7/8] Benchmarking hash operations...")
        hash_result = self.benchmark_hash_operations(num_operations=10000)
        print(f"      {hash_result.operations} ops in {hash_result.duration_seconds:.2f}s")
        print(f"      {hash_result.ops_per_second:.2f} ops/sec")
        
        print("\n[8/8] Benchmarking node ID generation...")
        node_id_result = self.benchmark_node_id_generation(num_operations=1000)
        print(f"      {node_id_result.operations} ops in {node_id_result.duration_seconds:.2f}s")
        print(f"      {node_id_result.ops_per_second:.2f} ops/sec")
        
        print("\n" + "=" * 60)
        print("Benchmark Summary")
        print("=" * 60)
        
        for name, result in self.results.items():
            print(f"\n{name}:")
            print(f"  Operations: {result.operations}")
            print(f"  Duration: {result.duration_seconds:.2f}s")
            print(f"  Throughput: {result.ops_per_second:.2f} ops/sec")
            print(f"  Latency: {result.ms_per_op:.3f} ms/op")
            if result.errors > 0:
                print(f"  Errors: {result.errors}")
                
        for name, result in self.latency_results.items():
            print(f"\n{name} (latency):")
            print(f"  Samples: {result.count}")
            print(f"  Mean: {result.mean:.3f}ms")
            print(f"  Median: {result.median:.3f}ms")
            print(f"  P95: {result.p95:.3f}ms")
            print(f"  P99: {result.p99:.3f}ms")
            print(f"  Min: {result.min_latency:.3f}ms")
            print(f"  Max: {result.max_latency:.3f}ms")
            if result.errors > 0:
                print(f"  Errors: {result.errors}")
                
        return self.results
        
    def export_results(self, filename: str = "benchmark_results.json"):
        """Export benchmark results to JSON."""
        data = {
            'throughput': {name: r.to_dict() for name, r in self.results.items()},
            'latency': {name: r.to_dict() for name, r in self.latency_results.items()}
        }
        
        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)
            
        print(f"\nResults exported to {filename}")


# Import KeyPair for benchmarks
from aether.crypto import KeyPair


def run_benchmarks():
    """Run benchmarks from command line."""
    suite = BenchmarkSuite()
    
    try:
        suite.run_all_benchmarks()
        suite.export_results()
    finally:
        suite.cleanup()


if __name__ == "__main__":
    run_benchmarks()
