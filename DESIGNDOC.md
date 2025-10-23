# Terminal Emulator Design Document

## Overview

This document describes the design and implementation of a feature-rich terminal emulator built using X11, supporting multiple tabs, command execution, pipes, redirection, and advanced shell features.

## Architecture

The terminal uses an event-driven architecture with:
- **X11 Event Loop**: Handles window events, keyboard input, and drawing
- **Tab Management**: Independent terminal sessions per tab
- **Process Management**: Fork/exec for command execution with pipe handling
- **Text Buffer**: Virtual screen buffer for display management

## Data Structures

### Core Structures

```c
typedef struct {
    wchar_t text_buffer[BUFFER_ROWS][BUFFER_COLS];  // Display buffer
    wchar_t current_command[MAX_COMMAND_LENGTH];    // Current input line
    wchar_t command_history[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH]; // Command history
    pid_t foreground_pid;                           // Current foreground process
    // ... other fields for cursor, search mode, etc.
} Tab;

typedef struct {
    pid_t pid;                     // Process ID
    int fd;                        // File descriptor for output
    char command[MAX_COMMAND_LENGTH]; // Command being watched
    char temp_file[64];            // Temporary output file
    int active;                    // Whether process is active
} MultiWatchProcess;
```

### Global State

```c
Tab tabs[MAX_TABS];                // Array of terminal tabs
int tab_count;                     // Current number of tabs
int active_tab_index;              // Currently active tab
MultiWatchProcess multiwatch_processes[MAX_MULTIWATCH_COMMANDS]; // multiWatch processes
```

---

## Feature Implementation Details

### 1. X11 Environment & Basic GUI
**Design Approach:** Create a simple X11 window with text rendering capabilities using a character grid buffer.

**Key System Calls:**
- `XOpenDisplay()` - Connect to X server
- `XCreateSimpleWindow()` - Create window
- `XMapWindow()` - Make window visible
- `XNextEvent()` - Event loop
- `XDrawString()` - Render text

**Implementation:** Basic event loop handles `Expose`, `KeyPress`, and `ButtonPress` events. Text is drawn from buffer.

### 2. Text Rendering & Buffer Management
Maintain a virtual text buffer with scrolling and Unicode support.

### 3. Keyboard Input & Command Line Editing
Handle X11 key events, history navigation, and Ctrl+A/E shortcuts.

### 4. Command Execution & Process Management
Fork/exec model with pipe communication between parent and child.

### 5. Input/Output Redirection
Parse `<` and `>` and redirect stdin/stdout accordingly.

### 6. Pipes Between Commands
Support multiple pipes for complex command chains.

### 7. Built-in cd Command
Executed in parent using `chdir()` and `getcwd()`.

### 8. Line Navigation (Ctrl+A, Ctrl+E)
Cursor movement implemented via keyboard event mapping.

### 9. Signal Handling (Ctrl+C, Ctrl+Z)
Parent captures signals and forwards to foreground process.

### 10. History Feature & Reverse Search
Command history with Ctrl+R incremental reverse search.

### 11. Auto-complete (Tab Key)
Filesystem scanning via `opendir()` and `readdir()`.

### 12. Multiple Tabs
Each tab maintains independent state: buffer, history, process list.

### 13. MultiWatch Command
Execute multiple commands simultaneously and watch outputs.

### 14. Unicode Support
Use `wchar_t` and conversion via `mbstowcs()`/`wcstombs()`.

### 15. Robustness & Error Handling
Comprehensive checks for all system calls with descriptive errors.

---

## Key Challenges & Solutions

- **Process Management:** Used `waitpid()` with `WNOHANG` for responsiveness.
- **Unicode Handling:** Fallback when conversions fail.
- **MultiWatch Performance:** Used `poll()` for multiple file monitoring.
- **Tab Isolation:** Each tab fully independent.
- **Error Recovery:** Graceful error reporting.

---

## Performance Considerations
- Non-blocking I/O for reading output
- Efficient redraw only when needed
- Fixed-size buffers to prevent exhaustion

---

## Limitations
- Fixed buffer (25x80)
- Max 10 tabs
- No scrollback
- Basic rendering only

---

## Future Enhancements
- Configurable buffer sizes
- Scrollback buffer
- Color output
- Custom key bindings
- Session persistence
