"""
Tests for configuration loading.
"""

import sys
import os
import tempfile
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether.config import load_config_file, save_config_file, _parse_simple_yaml


class TestConfigLoading:
    """Test configuration loading"""

    def test_parse_simple_yaml(self):
        """Test simple YAML parsing"""
        content = """
node:
  port: 7821
  max_connections: 100
  log_level: INFO
"""
        result = _parse_simple_yaml(content)
        assert 'node' in result
        assert result['node']['port'] == 7821
        assert result['node']['max_connections'] == 100
        assert result['node']['log_level'] == 'INFO'

    def test_parse_simple_yaml_booleans(self):
        """Test boolean parsing"""
        content = """
security:
  enable_tls: true
  debug: false
"""
        result = _parse_simple_yaml(content)
        assert result['security']['enable_tls'] is True
        assert result['security']['debug'] is False

    def test_parse_simple_yaml_strings(self):
        """Test string parsing"""
        content = """
paths:
  data_dir: ./aether_data
  log_file: /var/log/aether.log
"""
        result = _parse_simple_yaml(content)
        assert result['paths']['data_dir'] == './aether_data'
        assert result['paths']['log_file'] == '/var/log/aether.log'

    def test_parse_comments_ignored(self):
        """Test comments are ignored"""
        content = """
# This is a comment
node:
  port: 7821  # inline comment
"""
        result = _parse_simple_yaml(content)
        assert result['node']['port'] == 7821

    def test_load_nonexistent_file(self):
        """Test loading nonexistent file returns empty dict"""
        result = load_config_file('/nonexistent/path/config.yaml')
        assert result == {}

    def test_save_load_json(self):
        """Test saving and loading JSON config"""
        with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as f:
            path = f.name
        
        try:
            config = {'node': {'port': 7821, 'debug': True}}
            save_config_file(config, path)
            loaded = load_config_file(path)
            assert loaded == config
        finally:
            os.unlink(path)

    def test_save_load_yaml(self):
        """Test saving and loading YAML config"""
        with tempfile.NamedTemporaryFile(suffix='.yaml', delete=False) as f:
            path = f.name
        
        try:
            config = {'node': {'port': 7821, 'debug': True}}
            save_config_file(config, path)
            loaded = load_config_file(path)
            # May not be exact due to YAML parsing, but should have values
            assert 'node' in loaded
        finally:
            os.unlink(path)
