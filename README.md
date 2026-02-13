# 🎮 Dama Italiana — MCTS AI Player

An AI player for **Italian Checkers** (Dama Italiana) powered by **Monte Carlo Tree Search** with UCB1 and PUCT exploration policies, written in C11 with an SDL2 graphical interface.

![C](https://img.shields.io/badge/Language-C11-blue?logo=c)
![CMake](https://img.shields.io/badge/Build-CMake_3.16+-064F8C?logo=cmake)
![SDL2](https://img.shields.io/badge/Graphics-SDL2-blue?logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPj/HwADBwIAMCbHYQAAAABJRU5ErkJggg==)
![License](https://img.shields.io/badge/License-MIT-green)

---

## ✨ Features

- **MCTS AI Engine** — Full Monte Carlo Tree Search with configurable time budgets (0.2s, 1.0s, 3.0s)
- **Dual Policies** — UCB1 (Upper Confidence Bound) and PUCT (Predictor + UCT) exploration strategies
- **Bitboard Representation** — Compact 32-bit board representation for fast move generation
- **Memory Pool Allocator** — Custom arena allocator for efficient MCTS tree node management
- **Italian Rules Compliant** — Mandatory capture, capture priority rules (quantity, king captures, king moves)
- **Graphical Interface** — SDL2-based GUI with interactive board, menu controls, and visual feedback
- **Abstract Renderer** — Pluggable rendering backend (SDL2 included, extensible to OpenGL/terminal)
- **Self-Play Mode** — AI vs AI matches for performance evaluation
- **Hyperparameter Tuning** — Genetic algorithm-based parameter optimization for Cp/Cpuct
- **Built-in Test Suite** — 10 correctness tests covering game logic, move generation, and MCTS

## 📁 Project Structure

```
├── CMakeLists.txt          # Build configuration
├── CMakePresets.json        # VS2022 / Ninja presets
├── include/
│   ├── types.h              # Core types: Bitboard, GameState, Move
│   ├── bitboard.h           # Bitboard utilities and Zobrist hashing
│   ├── movegen.h            # Legal move generation with Italian rules
│   ├── game.h               # Make/unmake move, result detection
│   ├── mcts.h               # MCTS engine with UCB1/PUCT
│   ├── pool.h               # Memory pool allocator
│   ├── renderer.h           # Abstract rendering interface (vtable)
│   ├── gui.h                # GUI controller (renderer-agnostic)
│   └── tuning.h             # Genetic algorithm tuning framework
├── src/
│   ├── main.c               # Entry point, argument parsing, run modes
│   ├── bitboard.c           # Square mapping, popcount, Zobrist keys
│   ├── movegen.c            # Move generation with capture priorities
│   ├── game.c               # Game state transitions, result checking
│   ├── mcts.c               # MCTS search: selection, expansion, rollout
│   ├── pool.c               # Arena-based node allocator
│   ├── renderer_sdl2.c      # SDL2 rendering backend implementation
│   ├── gui.c                # Event-driven game loop
│   └── tuning.c             # Round-robin tournaments, genetic tuning
└── deps/
    ├── SDL2-2.30.10/        # SDL2 development libraries (pre-built)
    └── SDL2_ttf-2.22.0/     # SDL2_ttf development libraries (pre-built)
```

## 🔧 Building

### Prerequisites

- **CMake** ≥ 3.16
- **MSVC** (Visual Studio 2022) or **GCC/Clang**
- SDL2 and SDL2_ttf are **bundled** in `deps/` — no additional installation needed

### Build with Visual Studio 2022

1. Open the folder in Visual Studio (File → Open → CMake...)
2. Select the **x64 Debug** or **x64 Release** preset
3. Build with **Ctrl+Shift+B**

### Build from Command Line

```bash
# Configure
cmake --preset x64-debug

# Build
cmake --build out/build/x64-debug

# Run
./out/build/x64-debug/dama_italiana.exe
```

## 🚀 Usage

```bash
dama_italiana [options]
```

### Run Modes

| Flag | Description |
|------|-------------|
| `--mode gui` | Interactive GUI game (default) |
| `--mode selfplay` | AI vs AI single game |
| `--mode tuning` | Hyperparameter tuning via genetic algorithm |
| `--mode test` | Run built-in correctness tests |

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--policy ucb1\|puct` | `ucb1` | MCTS exploration policy |
| `--time <seconds>` | `1.0` | AI time budget per move |
| `--cp <float>` | `√2 ≈ 1.414` | UCB1 exploration constant |
| `--cpuct <float>` | `1.0` | PUCT exploration constant |
| `--games <int>` | `10` | Games for selfplay/tuning |
| `--population <int>` | `8` | GA population size |
| `--generations <int>` | `5` | GA generation count |
| `--verbose` | off | Verbose output |

### Examples

```bash
# Play against AI with PUCT policy, 3 second thinking time
dama_italiana --policy puct --time 3.0

# Run 50 self-play games with UCB1
dama_italiana --mode selfplay --games 50 --policy ucb1

# Tune hyperparameters with GA
dama_italiana --mode tuning --population 12 --generations 10

# Run test suite
dama_italiana --mode test
```

## 🏛️ Architecture

```
┌──────────────────────────────────────────────┐
│                   main.c                     │
│         (CLI parsing, mode dispatch)         │
├──────────┬───────────┬───────────┬───────────┤
│  GUI     │ Self-Play │  Tuning   │   Test    │
│  Mode    │   Mode    │   Mode    │   Mode    │
├──────────┴───────────┴───────────┴───────────┤
│              gui.c (game loop)               │
│          ┌─────────────────────┐             │
│          │  Renderer (vtable)  │             │
│          │  ┌───────────────┐  │             │
│          │  │renderer_sdl2.c│  │             │
│          │  └───────────────┘  │             │
│          └─────────────────────┘             │
├──────────────────────────────────────────────┤
│              mcts.c (search engine)          │
│     ┌─────────────┐  ┌─────────────────┐    │
│     │ UCB1 Policy  │  │  PUCT Policy    │    │
│     └─────────────┘  └─────────────────┘    │
├──────────────────────────────────────────────┤
│  game.c     │  movegen.c    │  bitboard.c   │
│  (rules)    │  (moves)      │  (bitops)     │
├──────────────────────────────────────────────┤
│              pool.c (memory arena)           │
└──────────────────────────────────────────────┘
```

### Key Design Decisions

- **Bitboard engine** — All 32 dark squares mapped to bits in a `uint32_t` for fast set operations
- **Zobrist hashing** — 64-bit position hashing for instant repetition detection
- **Memory pool** — Arena allocator avoids per-node `malloc`/`free` overhead during search
- **Abstract renderer** — Function-pointer vtable decouples game logic from graphics backend
- **Italian capture rules** — Full implementation of mandatory capture with all priority tiebreakers

## 📊 MCTS Algorithm

The AI uses **Monte Carlo Tree Search** with four phases:

1. **Selection** — Traverse the tree using UCB1 or PUCT formula to balance exploration/exploitation
2. **Expansion** — Create a new child node for an untried move
3. **Rollout** — Play random moves until a terminal state (with material cutoff heuristic)
4. **Backpropagation** — Update win/visit statistics up the tree

**UCB1:** `score = w/n + Cp × √(ln(N)/n)`
**PUCT:** `score = w/n + Cpuct × prior × √(N) / (1 + n)`

## 📜 Italian Checkers Rules

- 8×8 board, pieces move on dark squares only
- Men move diagonally forward; kings move diagonally in all directions
- **Mandatory capture** — if a capture is available, it must be taken
- **Capture priority** (in order): most pieces captured → king captures over man → most enemy kings captured → first king captured earliest in sequence
- Men that reach the last row are promoted to kings
- A player wins when the opponent has no pieces or no legal moves
- Draw after 40 moves without capture or promotion

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file.
