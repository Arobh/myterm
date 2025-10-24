# MyTerm - Custom Terminal with X11 GUI - Design Document

## Overview
MyTerm is a feature-rich terminal emulator built using the X11 library, supporting multiple tabs, command execution, pipes, redirection, and advanced shell features.

---

## 🧱 Architecture
The terminal uses an event-driven architecture with:

- **X11 Event Loop**: Handles window events, keyboard input, and drawing  
- **Tab Management**: Independent terminal sessions per tab  
- **Process Management**: Fork/exec for command execution with pipe handling  
- **Text Buffer**: Virtual screen buffer for display management with scrollback support  

---

## 🧩 Core Data Structures

### Tab Structure
```c
typedef struct {
    wchar_t text_buffer[BUFFER_ROWS][BUFFER_COLS];  // Display buffer
    wchar_t current_command[MAX_COMMAND_LENGTH];    // Current input line
    wchar_t command_history[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH]; // Command history
    wchar_t scrollback_buffer[SCROLLBACK_LINES][BUFFER_COLS]; // Scrollback history
    pid_t foreground_pid;                           // Current foreground process
    int scrollback_count;                          // Lines in scrollback
    int scrollback_offset;                         // Scroll position
    // ... other fields for cursor, search mode, etc.
} Tab;
```

### Process Management Structures
```c
typedef struct {
    pid_t pid;                     // Process ID
    char status[16];              // "Running" or "Stopped"
    char command[MAX_COMMAND_LENGTH];
    int job_id;
} BGProcess;

typedef struct {
    pid_t pid;
    int fd;                       // File descriptor for output
    char command[MAX_COMMAND_LENGTH];
    char temp_file[64];          // Temporary output file
    int active;
} MultiWatchProcess;
```

---

## ⚙️ Feature Implementation Details

### 1. X11 Environment & Basic GUI
**Design:** Created a simple X11 window with text rendering using a character grid buffer.

**Key System Calls:**
- `XOpenDisplay()`  
- `XCreateSimpleWindow()`  
- `XMapWindow()`  
- `XNextEvent()`  
- `XDrawString()`  

**Implementation:** Event loop handles Expose, KeyPress, and ButtonPress events. Text is drawn from buffer with Unicode support.

---

### 2. Text Rendering & Buffer Management
- Maintains 25x80 virtual buffer with 1000-line scrollback.
- Unicode via `setlocale()` and `wchar_t` buffers.
- Scrollback with mouse wheel and keyboard.

---

### 3. Keyboard Input & Command Editing
- Handles normal input, control keys, tab completion, and history navigation.
- Uses `XLookupString()` for key translation.
- Maintains cursor position and input buffer editing.

---

### 4. Command Execution & Process Management
- **Fork/exec model** with output redirection.  
- Background process (`&`) support and signal handling.

**Key System Calls:** `fork()`, `execvp()`, `pipe()`, `waitpid()`

---

### 5. Input/Output Redirection
- Parses `<`, `>`, and handles redirection via `dup2()` and `open()`.
- Supports combined redirection like `cmd < in > out`.

---

### 6. Pipes Between Commands
- Supports multi-stage pipelines (`cmd1 | cmd2 | cmd3`).
- Creates N−1 pipes, connects stdin/stdout appropriately.
- Uses process groups for proper signal control.

---

### 7. Built-in Commands
- **cd**: Uses `chdir()` in parent process.  
- **history**: Shows recent commands.  
- **jobs**, **fg**: Manage background processes.

---

### 8. Line Navigation (Ctrl+A, Ctrl+E)
- Bash-style editing:
  - Ctrl+A → Move to line start
  - Ctrl+E → Move to line end

---

### 9. Signal Handling (Ctrl+C, Ctrl+Z)
- Parent captures signals and forwards to child processes.
- **SIGINT** → Interrupt foreground process  
- **SIGTSTP** → Stop process and send to background

---

### 10. History Feature & Reverse Search
- Maintains 10,000 command entries.  
- Ctrl+R reverse search with substring matching.

---

### 11. Auto-complete (Tab Key)
- Filesystem scanning via `opendir()`/`readdir()`.
- Supports multiple matches and common prefix completion.

---

### 12. Multiple Tabs
- Each tab has independent state (buffer, history, jobs).  
- **Ctrl+N**: New tab  
- **Ctrl+W**: Close tab  
- **Ctrl+Tab**: Switch tab  

---

### 13. MultiWatch Command
- Monitors multiple commands in parallel using `poll()`.  
- Outputs displayed with timestamps.

**Key System Calls:** `fork()`, `poll()`, `open()`, `read()`

---

### 14. Scrollback Buffer
- Stores 1000 lines of output.  
- Page Up/Down, mouse wheel, Home/End navigation.

---

### 15. Unicode Support
- `setlocale(LC_ALL, "")` for locale setup.  
- Wide-character (`wchar_t`) buffers.  
- `mbstowcs()` and `wcstombs()` for conversions.

---

### 16. Error Handling & Robustness
- Error checking for every system call.  
- Buffer overflow prevention.  
- Resource cleanup and graceful recovery.

---

## 🧠 Key Challenges & Solutions

| Challenge | Solution |
|------------|-----------|
| Process Management | Used `waitpid()` with `WNOHANG` for non-blocking waits |
| Unicode Handling | Fallback to ASCII when conversion fails |
| MultiWatch Efficiency | Used `poll()` for multi-descriptor monitoring |
| Tab Isolation | Independent data per tab |
| Scrollback | Circular buffer implementation |
| Signal Races | Proper masking and atomic flags |

---

## ⚡ Performance Considerations
- Non-blocking I/O for responsiveness  
- Efficient redraw only on change  
- Fixed-size buffers to prevent leaks  
- Poll-based I/O instead of threading  

---

## 🔒 Security Features
- Command validation to block dangerous patterns  
- Safe temp file creation for multiWatch  
- Buffer bounds checking  
- Graceful process termination  

---

## ⚠️ Limitations
- Fixed 25x80 buffer size  
- Maximum 10 tabs  
- No advanced text styling  
- Works only in X11 environment  

---

## 🌱 Future Enhancements
- Configurable buffer size and layout  
- Color output and ANSI support  
- Customizable key bindings  
- Session persistence  
- Plugin API for extensibility  
- Better font rendering and scaling  
- Network transparency  

---
