# A.E.T.H.E.R. Root Makefile
# Provides convenient targets for both C/C++ and Python implementations

.PHONY: all cpp python test clean help install cpp-debug python-dev

# Default target - build both
all: cpp python-dev

# Build C++ implementation
cpp:
	@echo "Building C++ implementation..."
	$(MAKE) -C cpp

# Build C++ debug
cpp-debug:
	@echo "Building C++ implementation (debug)..."
	$(MAKE) -C cpp debug

# Setup Python implementation
python-dev:
	@echo "Setting up Python implementation..."
	cd python && pip install -r requirements.txt
	cd python && pip install -e .

python:
	@echo "Python implementation ready (no build required)"

# Run tests for both implementations
test: test-cpp test-python

test-cpp:
	@echo "Running C++ tests..."
	$(MAKE) -C cpp test

test-python:
	@echo "Running Python tests..."
	cd python && pytest tests/

# Clean both implementations
clean: clean-cpp clean-python

clean-cpp:
	@echo "Cleaning C++ implementation..."
	$(MAKE) -C cpp clean

clean-python:
	@echo "Cleaning Python implementation..."
	cd python && find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	cd python && find . -type f -name "*.pyc" -delete 2>/dev/null || true
	cd python && rm -rf *.egg-info 2>/dev/null || true

# Install both implementations
install: install-cpp install-python

install-cpp:
	@echo "Installing C++ implementation..."
	$(MAKE) -C cpp install

install-python:
	@echo "Installing Python implementation..."
	cd python && pip install .

# Format code
format: format-cpp format-python

format-cpp:
	@echo "Formatting C++ code..."
	-clang-format -i cpp/src/*.cpp cpp/include/*.hpp 2>/dev/null || echo "clang-format not available"

format-python:
	@echo "Formatting Python code..."
	cd python && black aether/ tests/ 2>/dev/null || echo "black not available"

# Lint code
lint: lint-cpp lint-python

lint-cpp:
	@echo "Linting C++ code..."
	-cppcheck --enable=all cpp/src/*.cpp cpp/include/*.hpp 2>/dev/null || echo "cppcheck not available"

lint-python:
	@echo "Linting Python code..."
	cd python && flake8 aether/ tests/ 2>/dev/null || echo "flake8 not available"

# Help
help:
	@echo "A.E.T.H.E.R. P2P Network - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build both implementations (default)"
	@echo "  cpp          - Build C++ implementation"
	@echo "  cpp-debug    - Build C++ implementation (debug)"
	@echo "  python       - Setup Python implementation"
	@echo "  python-dev   - Setup Python with dev dependencies"
	@echo "  test         - Run tests for both"
	@echo "  test-cpp     - Run C++ tests"
	@echo "  test-python  - Run Python tests"
	@echo "  clean        - Clean both implementations"
	@echo "  clean-cpp    - Clean C++ implementation"
	@echo "  clean-python - Clean Python implementation"
	@echo "  install      - Install both implementations"
	@echo "  format       - Format code"
	@echo "  lint         - Lint code"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Directory-specific:"
	@echo "  cd cpp && make   - C++ build commands"
	@echo "  cd python && pip install -e .  - Python install"
