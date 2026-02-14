#!/usr/bin/env python3
"""
Validate CRC-16 checksum for HDLC frame used in Norwegian HAN M-Bus protocol.
Uses standard HDLC CRC-16 polynomial (0x1021), initial value 0xFFFF, final XOR 0xFFFF.
"""

def crc16_hdlc(data):
    """
    Calculate CRC-16 using HDLC parameters:
    - Polynomial: 0x1021 (x^16 + x^12 + x^5 + 1)
    - Initial value: 0xFFFF
    - Final XOR: 0xFFFF
    - Reflected: No
    """
    crc = 0xFFFF
    
    for byte in data:
        crc ^= byte << 8
        
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            
            # Keep CRC as 16-bit value
            crc &= 0xFFFF
    
    # Final XOR
    crc ^= 0xFFFF
    
    return crc

def validate_hdlc_crc():
    # Frame data as hex string
    frame_hex = "A1:8A:08:83:13:FD:E6:40:01:02:02:01:01:02:0B:49:4F:31:02:02:01:10:37:32:31:38:31:02:02:01:07:04:34:02:01:01:07:01:75:02:02:16:02:01:02:07:02:02:16:02:01:07:02:02:16:02:01:04:07:02:02:16:02:01:1F:07:10:07:02:02:16:02:01:07:10:01:02:02:16:02:01:07:10:02:02:02:16:02:01:20:07:08:F2:02:02:16:23:02:01:34:07:08:F7:02:02:16:23:02:01:07:02:02:16:23:02:02:01:07:E9:07:04:16:80:02:01:01:08:2A:02:02:01:16:02:01:02:08:02:02:01:16:02:01:08:B6:02:02:01:16:20:02:01:04:08:58:02:02:01:16:20:DC:F2"
    
    # Convert hex string to list of bytes
    hex_bytes = frame_hex.split(':')
    
    # Extract data bytes (excluding last 2 CRC bytes)
    data_bytes = [int(b, 16) for b in hex_bytes[:-2]]
    
    # Extract provided CRC (last 2 bytes)
    provided_crc_bytes = hex_bytes[-2:]
    provided_crc = int(provided_crc_bytes[0], 16) << 8 | int(provided_crc_bytes[1], 16)
    
    print(f"Frame length: {len(hex_bytes)} bytes")
    print(f"Data length: {len(data_bytes)} bytes")
    print(f"Provided CRC: {provided_crc_bytes[0]}:{provided_crc_bytes[1]} (0x{provided_crc:04X})")
    
    # Calculate CRC
    calculated_crc = crc16_hdlc(data_bytes)
    
    # Extract high and low bytes of calculated CRC
    calc_high = (calculated_crc >> 8) & 0xFF
    calc_low = calculated_crc & 0xFF
    
    print(f"Calculated CRC: {calc_high:02X}:{calc_low:02X} (0x{calculated_crc:04X})")
    
    # Validate
    is_valid = calculated_crc == provided_crc
    
    print(f"\nCRC Validation: {'VALID' if is_valid else 'INVALID'}")
    
    if not is_valid:
        print(f"Expected: 0x{provided_crc:04X}")
        print(f"Got: 0x{calculated_crc:04X}")
    
    return is_valid

if __name__ == "__main__":
    validate_hdlc_crc()