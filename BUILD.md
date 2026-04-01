# A.E.T.H.E.R. Node - CI Build

[![Build Status](../../actions/workflows/build.yml/badge.svg)](../../actions)

Pre-built binaries are available on the [Releases](../../releases) page.

## Download Pre-built Binaries

### From Releases
1. Go to [Releases](../../releases)
2. Download the archive for your platform:
   - `aether-node-windows-x64.zip` - Windows
   - `aether-node-linux-x64.zip` - Linux
   - `aether-node-macos-x64.zip` - macOS

### From GitHub Actions (Development Builds)
1. Go to [Actions](../../actions)
2. Click on the latest workflow run
3. Download the artifact for your platform

## Build from Source

### Prerequisites

**Windows:**
- MSYS2 with MinGW-w64
```bash
# Install MSYS2 from https://www.msys2.org/
# Then in MSYS2 UCRT64 shell:
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
```

**Linux:**
```bash
# Debian/Ubuntu
sudo apt-get install build-essential

# Fedora/RHEL
sudo dnf groupinstall "Development Tools"
```

**macOS:**
```bash
xcode-select --install
```

### Build Commands

```bash
# All platforms
make

# Windows (in MSYS2 shell)
make windows

# Linux
make linux

# macOS
make macos

# Debug build
make debug

# Clean
make clean
```

### Output

- `bin/aether-node.exe` (Windows)
- `bin/aether-node` (Linux/macOS)

## Usage

```bash
# Run with defaults
./aether-node

# Custom port
./aether-node --port 8080

# Custom data directory
./aether-node --datadir /var/lib/aether

# Verbose logging
./aether-node --verbose

# Show help
./aether-node --help
```

## Configuration

Create `aether.toml`:

```toml
# Node identity (generated on first run)
identity_path = "aether_data/identity.bin"

# Network
listen_port = 7821
max_connections = 10000

# Data
data_dir = "aether_data"

# Logging
log_level = "info"  # trace, debug, info, warn, error
```

## Verify Build (Reproducible)

```bash
# Build locally
make clean && make

# Compare hash with CI artifact
sha256sum bin/aether-node
```

## License

MIT License
