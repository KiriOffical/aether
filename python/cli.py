#!/usr/bin/env python3
"""
A.E.T.H.E.R. Python - Main CLI entry point.
"""

import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether.node import main

if __name__ == '__main__':
    sys.exit(main())
