#!/usr/bin/env python3
"""
CRC-16 validator for HDLC frames in Norwegian HAN M-Bus protocol
"""

def crc16_hdlc(data):
    """
    Calculate CRC-16 for HDLC frames
    Polynomial: 0x1021 (x^16 + x^12 + x^5 + 1)
    Initial value: 0xFFFF
    Final XOR: 0xFFFF
    """
    crc = 0xFFFF
    
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    
    return crc ^ 0xFFFF

def validate_frame(hex_string):
    """
    Validate CRC-16 checksum for a hex string frame
    """
    # Remove colons and convert to bytes
    hex_clean = hex_string.replace(':', '').replace(' ', '')
    
    if len(hex_clean) % 2 != 0:
        print("Error: Invalid hex string length")
        return False
    
    # Convert hex to bytes
    frame_bytes = bytes.fromhex(hex_clean)
    
    if len(frame_bytes) < 3:
        print("Error: Frame too short")
        return False
    
    # Split frame data and CRC
    frame_data = frame_bytes[:-2]  # All except last 2 bytes
    provided_crc = frame_bytes[-2:]  # Last 2 bytes
    
    # Calculate CRC
    calculated_crc = crc16_hdlc(frame_data)
    
    # Convert calculated CRC to bytes (big-endian)
    calculated_crc_bytes = calculated_crc.to_bytes(2, 'big')
    
    print(f"Frame length: {len(frame_bytes)} bytes")
    print(f"Data length: {len(frame_data)} bytes")
    print(f"Provided CRC: {provided_crc.hex().upper()}")
    print(f"Calculated CRC: {calculated_crc_bytes.hex().upper()}")
    print(f"CRC Valid: {provided_crc == calculated_crc_bytes}")
    
    return provided_crc == calculated_crc_bytes

if __name__ == "__main__":
    # Test frame 1
    frame1 = "A1:8A:08:83:13:FD:E6:40:01:02:02:01:01:02:0B:49:4F:31:02:02:01:10:37:32:31:38:31:02:02:01:07:04:34:02:01:01:07:01:75:02:02:16:02:01:02:07:02:02:16:02:01:07:02:02:16:02:01:04:07:02:02:16:02:01:1F:07:10:07:02:02:16:02:01:07:10:01:02:02:16:02:01:07:10:02:02:02:16:02:01:20:07:08:F2:02:02:16:23:02:01:34:07:08:F7:02:02:16:23:02:01:07:02:02:16:23:02:02:01:07:E9:07:04:16:80:02:01:01:08:2A:02:02:01:16:02:01:02:08:02:02:01:16:02:01:08:B6:02:02:01:16:20:02:01:04:08:58:02:02:01:16:20:DC:F2"
    
    print("=== Testing Frame 1 ===")
    validate_frame(frame1)
    
    print("\n=== Ready for additional frames ===")
    print("To test another frame, call: validate_frame('your_hex_string_here')")