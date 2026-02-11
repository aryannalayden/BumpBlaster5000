# BumpBlaster5000
Code for controlling Sam G3 based on behavior
## Setup with uv

This project uses [uv](https://docs.astral.sh/uv/) for dependency management.

### Installation

1. **Install uv** (if not already installed):
   ```bash
   pip install uv
   ```

2. **Set up the project environment**:
   ```bash
   cd BumpBlaster5000
   uv sync
   ```

3. **Install PyQt5** (optional, for GUI components):
   ```bash
   uv pip install PyQt5
   ```
   *Note: PyQt5 is installed separately due to dependency resolution constraints on Windows*

### Running Python in the Virtual Environment

Use `uv run` to execute commands in the managed virtual environment:
```bash
# Run a script
uv run python script.py

# Run Python interactively
uv run python

# Run a specific module
uv run python -m BumpBlaster5000.module_name
```

Alternatively, activate the virtual environment directly:
```bash
# Windows
.venv\Scripts\activate

# macOS/Linux
source .venv/bin/activate
```

### Managing Dependencies

To add a new dependency:
```bash
uv add package_name
```

To update dependencies:
```bash
uv sync
```