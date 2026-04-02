#!/bin/bash
set -e

# A.E.T.H.E.R. Node Entrypoint Script

# Default values
AETHER_PORT=${AETHER_PORT:-7821}
AETHER_DATA_DIR=${AETHER_DATA_DIR:-/aether_data}
AETHER_LOG_LEVEL=${AETHER_LOG_LEVEL:-INFO}
AETHER_MAX_CONNECTIONS=${AETHER_MAX_CONNECTIONS:-10000}
AETHER_BOOTSTRAP_PEERS=${AETHER_BOOTSTRAP_PEERS:-""}

# Parse command line arguments
COMMAND=${1:-run}

case "$COMMAND" in
    run)
        echo "Starting A.E.T.H.E.R. Node..."
        echo "  Port: $AETHER_PORT"
        echo "  Data Dir: $AETHER_DATA_DIR"
        echo "  Log Level: $AETHER_LOG_LEVEL"
        echo "  Max Connections: $AETHER_MAX_CONNECTIONS"
        
        # Build bootstrap peers argument
        BOOTSTRAP_ARG=""
        if [ -n "$AETHER_BOOTSTRAP_PEERS" ]; then
            BOOTSTRAP_ARG="--bootstrap $AETHER_BOOTSTRAP_PEERS"
        fi
        
        # Run the node
        exec python -m aether.node run \
            --port "$AETHER_PORT" \
            --datadir "$AETHER_DATA_DIR" \
            --log-level "$AETHER_LOG_LEVEL" \
            --max-connections "$AETHER_MAX_CONNECTIONS" \
            $BOOTSTRAP_ARG
        ;;
        
    shell)
        echo "Starting shell..."
        exec /bin/bash
        ;;
        
    version)
        python -c "from aether import __version__; print(__version__)"
        ;;
        
    *)
        echo "Usage: entrypoint.sh {run|shell|version}"
        exit 1
        ;;
esac
