"""
Data persistence and snapshot system for A.E.T.H.E.R.
Provides durable storage, snapshots, and recovery mechanisms.
"""

import os
import json
import time
import pickle
import hashlib
import threading
import shutil
import gzip
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Any, Set
from pathlib import Path
from datetime import datetime
import logging
import tempfile
import hmac


logger = logging.getLogger("aether.persistence")


@dataclass
class SnapshotMetadata:
    """Metadata for a snapshot."""
    snapshot_id: str
    created_at: float
    node_id: str
    dht_key_count: int
    peer_count: int
    size_bytes: int
    checksum: str
    version: str = "0.1.0"
    compression: str = "gzip"
    encrypted: bool = False
    
    def to_dict(self) -> dict:
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data: dict) -> 'SnapshotMetadata':
        return cls(**data)


@dataclass
class PersistenceConfig:
    """Configuration for persistence system."""
    data_dir: str = "aether_data"
    snapshot_dir: str = "snapshots"
    enable_snapshots: bool = True
    snapshot_interval: float = 3600  # 1 hour
    max_snapshots: int = 10
    enable_compression: bool = True
    enable_encryption: bool = False
    encryption_key: Optional[bytes] = None
    auto_cleanup: bool = True
    cleanup_threshold: float = 0.9  # Cleanup when 90% full


class ChecksumVerifier:
    """Verifies data integrity using checksums."""
    
    @staticmethod
    def calculate_checksum(data: bytes) -> str:
        """Calculate SHA-256 checksum of data."""
        return hashlib.sha256(data).hexdigest()
    
    @staticmethod
    def verify_checksum(data: bytes, expected_checksum: str) -> bool:
        """Verify data matches expected checksum."""
        actual = ChecksumVerifier.calculate_checksum(data)
        return hmac.compare_digest(actual, expected_checksum)


class DataEncoder:
    """Encodes and decodes data for storage."""
    
    @staticmethod
    def encode_key(key: bytes) -> str:
        """Encode key for storage."""
        return key.hex()
    
    @staticmethod
    def decode_key(encoded: str) -> bytes:
        """Decode key from storage."""
        return bytes.fromhex(encoded)
    
    @staticmethod
    def encode_value(value: bytes) -> dict:
        """Encode value for storage."""
        return {
            'data': value.hex(),
            'type': 'bytes'
        }
    
    @staticmethod
    def decode_value(encoded: dict) -> bytes:
        """Decode value from storage."""
        if encoded.get('type') == 'bytes':
            return bytes.fromhex(encoded['data'])
        return pickle.loads(bytes.fromhex(encoded['data']))


class SnapshotManager:
    """Manages snapshots of node state."""
    
    def __init__(self, config: PersistenceConfig):
        self.config = config
        self.snapshot_dir = os.path.join(config.data_dir, config.snapshot_dir)
        self._lock = threading.Lock()
        
        # Create snapshot directory
        os.makedirs(self.snapshot_dir, exist_ok=True)
        
    def create_snapshot(self, node_id: str, dht_data: Dict[bytes, Any],
                       peer_data: List[dict]) -> SnapshotMetadata:
        """
        Create a snapshot of current state.
        Returns metadata about the created snapshot.
        """
        with self._lock:
            timestamp = time.time()
            snapshot_id = f"snapshot_{int(timestamp)}"
            
            # Prepare snapshot data
            snapshot_data = {
                'metadata': {
                    'snapshot_id': snapshot_id,
                    'created_at': timestamp,
                    'node_id': node_id,
                    'version': "0.1.0"
                },
                'dht_data': {
                    DataEncoder.encode_key(k): v 
                    for k, v in dht_data.items()
                },
                'peer_data': peer_data
            }
            
            # Serialize to JSON
            json_data = json.dumps(snapshot_data, indent=2)
            data_bytes = json_data.encode('utf-8')
            
            # Compress if enabled
            if self.config.enable_compression:
                data_bytes = gzip.compress(data_bytes)
            
            # Calculate checksum
            checksum = ChecksumVerifier.calculate_checksum(data_bytes)
            
            # Create metadata
            metadata = SnapshotMetadata(
                snapshot_id=snapshot_id,
                created_at=timestamp,
                node_id=node_id,
                dht_key_count=len(dht_data),
                peer_count=len(peer_data),
                size_bytes=len(data_bytes),
                checksum=checksum,
                compression='gzip' if self.config.enable_compression else 'none',
                encrypted=self.config.enable_encryption
            )
            
            # Save snapshot
            snapshot_path = os.path.join(self.snapshot_dir, f"{snapshot_id}.snap")
            with open(snapshot_path, 'wb') as f:
                f.write(data_bytes)
            
            # Save metadata
            metadata_path = os.path.join(self.snapshot_dir, f"{snapshot_id}.meta.json")
            with open(metadata_path, 'w') as f:
                json.dump(metadata.to_dict(), f, indent=2)
            
            logger.info(f"Created snapshot {snapshot_id} ({len(data_bytes)} bytes)")
            
            # Cleanup old snapshots
            if self.config.auto_cleanup:
                self._cleanup_old_snapshots()
            
            return metadata
    
    def load_snapshot(self, snapshot_id: str) -> Optional[dict]:
        """Load a snapshot by ID."""
        snapshot_path = os.path.join(self.snapshot_dir, f"{snapshot_id}.snap")
        metadata_path = os.path.join(self.snapshot_dir, f"{snapshot_id}.meta.json")
        
        if not os.path.exists(snapshot_path):
            logger.error(f"Snapshot not found: {snapshot_id}")
            return None
        
        try:
            # Load metadata
            with open(metadata_path, 'r') as f:
                metadata = SnapshotMetadata.from_dict(json.load(f))
            
            # Load and decompress data
            with open(snapshot_path, 'rb') as f:
                data = f.read()
            
            # Verify checksum
            if not ChecksumVerifier.verify_checksum(data, metadata.checksum):
                logger.error(f"Snapshot checksum mismatch: {snapshot_id}")
                return None
            
            # Decompress if needed
            if metadata.compression == 'gzip':
                data = gzip.decompress(data)
            
            # Parse JSON
            snapshot_data = json.loads(data.decode('utf-8'))
            
            # Decode DHT data
            dht_data = {
                DataEncoder.decode_key(k): v 
                for k, v in snapshot_data.get('dht_data', {}).items()
            }
            
            return {
                'metadata': metadata,
                'dht_data': dht_data,
                'peer_data': snapshot_data.get('peer_data', [])
            }
            
        except Exception as e:
            logger.error(f"Failed to load snapshot: {e}")
            return None
    
    def list_snapshots(self) -> List[SnapshotMetadata]:
        """List all available snapshots."""
        snapshots = []
        
        for filename in os.listdir(self.snapshot_dir):
            if filename.endswith('.meta.json'):
                try:
                    with open(os.path.join(self.snapshot_dir, filename), 'r') as f:
                        metadata = SnapshotMetadata.from_dict(json.load(f))
                        snapshots.append(metadata)
                except Exception as e:
                    logger.debug(f"Failed to load snapshot metadata: {e}")
        
        # Sort by creation time (newest first)
        snapshots.sort(key=lambda s: s.created_at, reverse=True)
        return snapshots
    
    def delete_snapshot(self, snapshot_id: str) -> bool:
        """Delete a snapshot."""
        snapshot_path = os.path.join(self.snapshot_dir, f"{snapshot_id}.snap")
        metadata_path = os.path.join(self.snapshot_dir, f"{snapshot_id}.meta.json")
        
        deleted = False
        
        if os.path.exists(snapshot_path):
            os.remove(snapshot_path)
            deleted = True
        
        if os.path.exists(metadata_path):
            os.remove(metadata_path)
            deleted = True
        
        if deleted:
            logger.info(f"Deleted snapshot {snapshot_id}")
        
        return deleted
    
    def _cleanup_old_snapshots(self):
        """Remove old snapshots beyond max_snapshots limit."""
        snapshots = self.list_snapshots()
        
        if len(snapshots) > self.config.max_snapshots:
            for snapshot in snapshots[self.config.max_snapshots:]:
                self.delete_snapshot(snapshot.snapshot_id)
    
    def get_latest_snapshot(self) -> Optional[SnapshotMetadata]:
        """Get the most recent snapshot."""
        snapshots = self.list_snapshots()
        return snapshots[0] if snapshots else None
    
    def restore_latest(self) -> Optional[dict]:
        """Restore from the latest snapshot."""
        latest = self.get_latest_snapshot()
        if latest:
            return self.load_snapshot(latest.snapshot_id)
        return None


class PersistentStorage:
    """
    Persistent storage for DHT data and peer information.
    Uses write-ahead logging for durability.
    """
    
    def __init__(self, config: PersistenceConfig):
        self.config = config
        self.data_dir = Path(config.data_dir)
        self.wal_path = self.data_dir / "write_ahead.log"
        self.index_path = self.data_dir / "index.json"
        self._lock = threading.RLock()
        self._data: Dict[str, Any] = {}
        self._wal_buffer: List[dict] = []
        self._wal_size = 0
        self._wal_threshold = 1024 * 1024  # 1MB
        
        # Initialize storage
        self._init_storage()
        
    def _init_storage(self):
        """Initialize storage directories and load existing data."""
        self.data_dir.mkdir(parents=True, exist_ok=True)
        
        # Load index
        if self.index_path.exists():
            try:
                with open(self.index_path, 'r') as f:
                    self._data = json.load(f)
                logger.info(f"Loaded {len(self._data)} entries from storage")
            except Exception as e:
                logger.error(f"Failed to load index: {e}")
                self._data = {}
        
        # Replay WAL if exists
        if self.wal_path.exists():
            self._replay_wal()
    
    def _replay_wal(self):
        """Replay write-ahead log to recover uncommitted changes."""
        try:
            with open(self.wal_path, 'r') as f:
                for line in f:
                    entry = json.loads(line.strip())
                    op = entry.get('op')
                    key = entry.get('key')
                    
                    if op == 'set' and key:
                        self._data[key] = entry.get('value')
                    elif op == 'delete' and key:
                        self._data.pop(key, None)
            
            logger.info(f"Replayed {len(self._data)} entries from WAL")
            
            # Clear WAL after replay
            self.wal_path.unlink()
            
        except Exception as e:
            logger.error(f"WAL replay failed: {e}")
    
    def _write_wal(self, op: str, key: str, value: Any = None):
        """Write operation to write-ahead log."""
        entry = {
            'op': op,
            'key': key,
            'value': value,
            'timestamp': time.time()
        }
        
        self._wal_buffer.append(entry)
        self._wal_size += len(json.dumps(entry))
        
        # Flush if threshold reached
        if self._wal_size >= self._wal_threshold:
            self._flush_wal()
    
    def _flush_wal(self):
        """Flush WAL buffer to disk."""
        if not self._wal_buffer:
            return
        
        try:
            with open(self.wal_path, 'a') as f:
                for entry in self._wal_buffer:
                    f.write(json.dumps(entry) + '\n')
            
            self._wal_buffer = []
            self._wal_size = 0
            
        except Exception as e:
            logger.error(f"WAL flush failed: {e}")
    
    def set(self, key: bytes, value: Any, metadata: Optional[dict] = None):
        """Store a value persistently."""
        key_str = DataEncoder.encode_key(key)
        
        with self._lock:
            # Write to WAL first
            self._write_wal('set', key_str, {
                'value': value,
                'metadata': metadata,
                'created_at': time.time()
            })
            
            # Then update in-memory data
            self._data[key_str] = {
                'value': value,
                'metadata': metadata or {},
                'created_at': time.time(),
                'updated_at': time.time()
            }
    
    def get(self, key: bytes) -> Optional[Any]:
        """Retrieve a value."""
        key_str = DataEncoder.encode_key(key)
        
        with self._lock:
            entry = self._data.get(key_str)
            if entry:
                return entry.get('value')
            return None
    
    def delete(self, key: bytes):
        """Delete a value."""
        key_str = DataEncoder.encode_key(key)
        
        with self._lock:
            # Write to WAL
            self._write_wal('delete', key_str)
            
            # Remove from data
            self._data.pop(key_str, None)
    
    def contains(self, key: bytes) -> bool:
        """Check if key exists."""
        key_str = DataEncoder.encode_key(key)
        return key_str in self._data
    
    def keys(self) -> List[bytes]:
        """Get all keys."""
        with self._lock:
            return [DataEncoder.decode_key(k) for k in self._data.keys()]
    
    def values(self) -> List[Any]:
        """Get all values."""
        with self._lock:
            return [entry.get('value') for entry in self._data.values()]
    
    def items(self) -> Dict[bytes, Any]:
        """Get all key-value pairs."""
        with self._lock:
            return {
                DataEncoder.decode_key(k): v.get('value')
                for k, v in self._data.items()
            }
    
    def count(self) -> int:
        """Get count of stored entries."""
        with self._lock:
            return len(self._data)
    
    def flush(self):
        """Flush all pending writes to disk."""
        with self._lock:
            self._flush_wal()
            
            # Save index
            with open(self.index_path, 'w') as f:
                json.dump(self._data, f, indent=2)
    
    def clear(self):
        """Clear all data."""
        with self._lock:
            self._data = {}
            self._wal_buffer = []
            self._wal_size = 0
            
            # Remove files
            if self.wal_path.exists():
                self.wal_path.unlink()
            if self.index_path.exists():
                self.index_path.unlink()
    
    def get_stats(self) -> dict:
        """Get storage statistics."""
        with self._lock:
            total_size = sum(
                len(json.dumps(entry)) 
                for entry in self._data.values()
            )
            
            return {
                'entry_count': len(self._data),
                'total_size_bytes': total_size,
                'wal_entries': len(self._wal_buffer),
                'wal_size_bytes': self._wal_size
            }


class DataManager:
    """
    High-level data manager combining persistence and snapshots.
    """
    
    def __init__(self, config: PersistenceConfig):
        self.config = config
        self.storage = PersistentStorage(config)
        self.snapshot_manager = SnapshotManager(config)
        self._snapshot_thread: Optional[threading.Thread] = None
        self._running = False
        
    def start(self):
        """Start background tasks."""
        self._running = True
        
        if self.config.enable_snapshots:
            self._snapshot_thread = threading.Thread(
                target=self._snapshot_loop,
                daemon=True
            )
            self._snapshot_thread.start()
            logger.info("Snapshot manager started")
    
    def stop(self):
        """Stop background tasks and flush data."""
        self._running = False
        
        if self._snapshot_thread:
            self._snapshot_thread.join(timeout=5.0)
        
        # Flush all pending writes
        self.storage.flush()
        logger.info("Data manager stopped")
    
    def _snapshot_loop(self):
        """Background loop for periodic snapshots."""
        while self._running:
            time.sleep(self.config.snapshot_interval)
            
            if not self._running:
                break
            
            try:
                self.create_snapshot()
            except Exception as e:
                logger.error(f"Snapshot failed: {e}")
    
    def store(self, key: bytes, value: bytes, metadata: Optional[dict] = None):
        """Store data."""
        self.storage.set(key, value.hex(), metadata)
    
    def retrieve(self, key: bytes) -> Optional[bytes]:
        """Retrieve data."""
        value = self.storage.get(key)
        if value:
            return bytes.fromhex(value)
        return None
    
    def remove(self, key: bytes):
        """Remove data."""
        self.storage.delete(key)
    
    def create_snapshot(self) -> Optional[SnapshotMetadata]:
        """Create a manual snapshot."""
        try:
            # Get all data
            dht_data = self.storage.items()
            
            # Create snapshot (peer_data would come from peer manager)
            metadata = self.snapshot_manager.create_snapshot(
                node_id="node_id_placeholder",
                dht_data=dht_data,
                peer_data=[]
            )
            
            return metadata
        except Exception as e:
            logger.error(f"Snapshot creation failed: {e}")
            return None
    
    def restore_snapshot(self, snapshot_id: str) -> bool:
        """Restore from a snapshot."""
        snapshot = self.snapshot_manager.load_snapshot(snapshot_id)
        
        if not snapshot:
            return False
        
        try:
            # Restore DHT data
            for key, value in snapshot['dht_data'].items():
                self.storage.set(key, value['value'], value.get('metadata'))
            
            # Flush to disk
            self.storage.flush()
            
            logger.info(f"Restored snapshot {snapshot_id}")
            return True
            
        except Exception as e:
            logger.error(f"Snapshot restore failed: {e}")
            return False
    
    def list_snapshots(self) -> List[SnapshotMetadata]:
        """List available snapshots."""
        return self.snapshot_manager.list_snapshots()
    
    def get_stats(self) -> dict:
        """Get comprehensive stats."""
        storage_stats = self.storage.get_stats()
        snapshots = self.snapshot_manager.list_snapshots()
        
        return {
            'storage': storage_stats,
            'snapshots': {
                'count': len(snapshots),
                'latest': snapshots[0].to_dict() if snapshots else None,
                'total_size': sum(s.size_bytes for s in snapshots)
            }
        }
    
    def export_data(self, format: str = 'json') -> bytes:
        """Export all data in specified format."""
        data = self.storage.items()
        
        if format == 'json':
            export_data = {
                key.hex(): value.hex() if isinstance(value, bytes) else value
                for key, value in data.items()
            }
            return json.dumps(export_data, indent=2).encode('utf-8')
        
        elif format == 'pickle':
            return pickle.dumps(data)
        
        else:
            raise ValueError(f"Unknown export format: {format}")
    
    def import_data(self, data: bytes, format: str = 'json'):
        """Import data from exported format."""
        if format == 'json':
            import_data = json.loads(data.decode('utf-8'))
            for key_str, value in import_data.items():
                key = bytes.fromhex(key_str)
                if isinstance(value, str):
                    value = bytes.fromhex(value)
                self.storage.set(key, value)
        
        elif format == 'pickle':
            import_data = pickle.loads(data)
            for key, value in import_data.items():
                self.storage.set(key, value)
        
        # Flush after import
        self.storage.flush()
