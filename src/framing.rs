//! Message framing layer for length-prefixed binary protocol

use bytes::{Buf, BufMut, BytesMut};
use byteorder::{BigEndian, ReadBytesExt, WriteBytesExt};
use std::io::Cursor;

use crate::error::{Error, Result};
use crate::message::{Message, MAX_FRAME_SIZE};

/// Minimum frame header size (4 bytes for length prefix)
pub const FRAME_HEADER_SIZE: usize = 4;

/// Frame encoder/decoder for length-prefixed messages
pub struct Framer {
    /// Maximum allowed frame size
    max_size: usize,
    /// Receive buffer
    recv_buffer: BytesMut,
    /// Current frame size being read (0 means reading header)
    current_frame_size: usize,
}

impl Framer {
    /// Create a new framer with default max size
    pub fn new() -> Self {
        Self {
            max_size: MAX_FRAME_SIZE,
            recv_buffer: BytesMut::with_capacity(8192),
            current_frame_size: 0,
        }
    }
    
    /// Create a new framer with custom max size
    pub fn with_max_size(max_size: usize) -> Self {
        Self {
            max_size,
            recv_buffer: BytesMut::with_capacity(8192),
            current_frame_size: 0,
        }
    }
    
    /// Encode a message into a framed buffer
    pub fn encode(&self, message: &Message) -> Result<BytesMut> {
        let payload = message.encode()?;
        
        if payload.len() > self.max_size {
            return Err(Error::MessageTooLarge(payload.len(), self.max_size));
        }
        
        let mut buffer = BytesMut::with_capacity(FRAME_HEADER_SIZE + payload.len());
        buffer.put_u32(payload.len() as u32);
        buffer.put_slice(&payload);
        
        Ok(buffer)
    }
    
    /// Add received data to the buffer
    pub fn receive(&mut self, data: &[u8]) {
        self.recv_buffer.extend_from_slice(data);
    }
    
    /// Try to decode a complete frame from the buffer
    /// Returns Ok(None) if no complete frame is available
    /// Returns Err if frame is invalid or too large
    pub fn decode(&mut self) -> Result<Option<Message>> {
        // Need at least header size to read frame length
        if self.recv_buffer.len() < FRAME_HEADER_SIZE && self.current_frame_size == 0 {
            return Ok(None);
        }
        
        // Read frame size if not already known
        if self.current_frame_size == 0 {
            let mut cursor = Cursor::new(&self.recv_buffer[..FRAME_HEADER_SIZE]);
            let frame_size = cursor.read_u32::<BigEndian>()? as usize;
            
            if frame_size > self.max_size {
                return Err(Error::FrameSizeExceeded(frame_size));
            }
            
            self.current_frame_size = frame_size;
        }
        
        // Check if we have the complete frame
        let total_size = FRAME_HEADER_SIZE + self.current_frame_size;
        if self.recv_buffer.len() < total_size {
            return Ok(None);
        }
        
        // Extract payload
        let payload_start = FRAME_HEADER_SIZE;
        let payload_end = payload_start + self.current_frame_size;
        let payload = self.recv_buffer[payload_start..payload_end].to_vec();
        
        // Advance buffer
        self.recv_buffer.advance(total_size);
        
        // Reset frame state
        self.current_frame_size = 0;
        
        // Decode message
        let message = Message::decode(&payload)?;
        Ok(Some(message))
    }
    
    /// Get the number of bytes currently in the buffer
    pub fn buffered_bytes(&self) -> usize {
        self.recv_buffer.len()
    }
    
    /// Clear the receive buffer
    pub fn clear(&mut self) {
        self.recv_buffer.clear();
        self.current_frame_size = 0;
    }
}

impl Default for Framer {
    fn default() -> Self {
        Self::new()
    }
}

/// Utility functions for frame operations
pub fn frame_message(message: &Message) -> Result<Vec<u8>> {
    let framer = Framer::new();
    let framed = framer.encode(message)?;
    Ok(framed.to_vec())
}

pub fn parse_frame(data: &[u8]) -> Result<(Message, usize)> {
    if data.len() < FRAME_HEADER_SIZE {
        return Err(Error::InvalidMessage("Insufficient data for frame header".to_string()));
    }
    
    let mut cursor = Cursor::new(data);
    let frame_size = cursor.read_u32::<BigEndian>()? as usize;
    
    if frame_size > MAX_FRAME_SIZE {
        return Err(Error::FrameSizeExceeded(frame_size));
    }
    
    let total_size = FRAME_HEADER_SIZE + frame_size;
    if data.len() < total_size {
        return Err(Error::InvalidMessage("Incomplete frame data".to_string()));
    }
    
    let payload = &data[FRAME_HEADER_SIZE..total_size];
    let message = Message::decode(payload)?;
    
    Ok((message, total_size))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::message::{Hello, Endpoint, Protocol as MsgProtocol, Ping};
    
    #[test]
    fn test_encode_decode() {
        let framer = Framer::new();
        
        let message = Message::Hello(Hello {
            version: 1,
            node_id: [1u8; 32],
            timestamp: 1234567890,
            protocols: vec!["/aether/1.0.0".to_string()],
            listen_addr: Endpoint {
                proto: MsgProtocol::Ip4,
                address: vec![127, 0, 0, 1],
                port: 7821,
            },
            signature: vec![0u8; 64],
        });
        
        let encoded = framer.encode(&message).unwrap();
        assert_eq!(encoded.len(), FRAME_HEADER_SIZE + message.encode().unwrap().len());
        
        // Verify length prefix
        let frame_size = encoded.get_u32() as usize;
        assert_eq!(frame_size, message.encode().unwrap().len());
    }
    
    #[test]
    fn test_framer_round_trip() {
        let mut framer = Framer::new();
        
        let message = Message::Ping(Ping { sequence: 42 });
        
        // Encode
        let encoded = framer.encode(&message).unwrap();
        
        // Decode
        framer.receive(&encoded);
        let decoded = framer.decode().unwrap().unwrap();
        
        match decoded {
            Message::Ping(pong) => assert_eq!(pong.sequence, 42),
            _ => panic!("Wrong message type"),
        }
        
        // Buffer should be empty
        assert_eq!(framer.buffered_bytes(), 0);
    }
    
    #[test]
    fn test_framer_partial_data() {
        let mut framer = Framer::new();
        
        let message = Message::Ping(Ping { sequence: 42 });
        let encoded = framer.encode(&message).unwrap();
        
        // Send only first half
        let mid = encoded.len() / 2;
        framer.receive(&encoded[..mid]);
        
        // Should not decode yet
        assert!(framer.decode().unwrap().is_none());
        
        // Send rest
        framer.receive(&encoded[mid..]);
        
        // Should decode now
        let decoded = framer.decode().unwrap().unwrap();
        match decoded {
            Message::Ping(pong) => assert_eq!(pong.sequence, 42),
            _ => panic!("Wrong message type"),
        }
    }
    
    #[test]
    fn test_frame_size_limit() {
        let framer = Framer::with_max_size(1024);
        
        // Create oversized message
        let message = Message::FragmentResponse(crate::message::FragmentResponse {
            result: crate::message::FragmentResult::Data {
                fragment_id: vec![0u8; 100],
                payload: vec![0u8; 2000], // Exceeds 1024 limit
                proof: vec![],
            },
        });
        
        let result = framer.encode(&message);
        assert!(matches!(result, Err(Error::MessageTooLarge(_, _))));
    }
}
