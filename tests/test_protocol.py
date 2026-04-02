"""
Tests for protocol message handling.
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether.protocol import Message, MessageType


class TestMessage:
    """Test Message class"""

    def test_create_message(self):
        """Test message creation"""
        msg = Message(MessageType.PING, {'sequence': 123})
        assert msg.type == MessageType.PING
        assert msg.payload == {'sequence': 123}

    def test_create_message_empty_payload(self):
        """Test message with empty payload"""
        msg = Message(MessageType.PING)
        assert msg.payload == {}

    def test_encode_decode(self):
        """Test message encoding and decoding"""
        original = Message(MessageType.STORE_VALUE, {
            'key': 'abc123',
            'value': 'test'
        })
        encoded = original.encode()
        decoded = Message.decode(encoded)
        
        assert decoded is not None
        assert decoded.type == original.type
        assert decoded.payload == original.payload

    def test_decode_invalid_data(self):
        """Test decoding invalid data"""
        result = Message.decode(b'invalid')
        assert result is None

    def test_decode_truncated(self):
        """Test decoding truncated data"""
        msg = Message(MessageType.PING, {'data': 'test'})
        encoded = msg.encode()
        result = Message.decode(encoded[:3])
        assert result is None

    def test_all_message_types(self):
        """Test all message types can be encoded/decoded"""
        for msg_type in MessageType:
            original = Message(msg_type, {'test': 'data'})
            encoded = original.encode()
            decoded = Message.decode(encoded)
            assert decoded is not None
            assert decoded.type == msg_type

    def test_large_payload(self):
        """Test message with large payload"""
        large_data = {'data': 'x' * 10000}
        msg = Message(MessageType.STORE_VALUE, large_data)
        encoded = msg.encode()
        decoded = Message.decode(encoded)
        assert decoded is not None
        assert decoded.payload == large_data
