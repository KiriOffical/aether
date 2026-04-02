# IPFS Node for Windows

## Files
- `ipfs-node.exe` - Main executable (480KB)
- `libsodium.dll` - Required cryptography library

## Usage

Open Command Prompt or PowerShell and run:

```cmd
# Show help
ipfs-node.exe help

# Start daemon (P2P node + HTTP gateway)
ipfs-node.exe daemon

# Start daemon with custom ports
ipfs-node.exe daemon --port 8080 --udp-port 4001 --tcp-port 4002

# Add a file
ipfs-node.exe add C:\path\to\file.txt

# Get a file by CID
ipfs-node.exe get <CID> C:\output\file.txt
```

## HTTP Gateway

Once the daemon is running, access files via:
```
http://localhost:8080/ipfs/<CID>
```

## Requirements

- Windows 10 or later (64-bit)
- Visual C++ Redistributable (for libsodium.dll)

## Firewall

On first run, Windows Firewall may ask for permission. Allow access for:
- UDP port 4001 (P2P discovery)
- TCP port 4002 (P2P data transfer)
- TCP port 8080 (HTTP gateway - local only)

## Data Storage

Files are stored in: `%USERPROFILE%\.my_ipfs\`

## Building from Source

```bash
# On Linux with MinGW-w64
make -f Makefile.win

# Output: bin/ipfs-node.exe
```

## Troubleshooting

**"libsodium.dll not found"**
- Copy libsodium.dll to the same folder as ipfs-node.exe
- Or add the folder to your PATH environment variable

**"Port already in use"**
- Another instance may be running
- Or another application uses port 8080/4001/4002
- Use custom ports: `--port 8081 --udp-port 4003 --tcp-port 4004`

**"Access denied"**
- Run as Administrator if binding to privileged ports
- Or use ports > 1024
