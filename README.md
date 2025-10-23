# X11 Terminal Emulator with Tabs

A feature-rich terminal emulator built with X11 that supports multiple tabs, command execution, pipes, redirection, and advanced features like multiWatch and Unicode support.

## Features

- **Multi-tab Interface**: Create, switch, and close tabs with mouse or keyboard
- **Command Execution**: Run shell commands with output capture
- **Pipes and Redirection**: Support for `|`, `<`, `>` operators
- **Advanced Features**:
  - Tab completion with file/directory suggestions
  - Command history with reverse-i-search (Ctrl+R)
  - Line editing (Ctrl+A, Ctrl+E, arrow keys)
  - Signal handling (Ctrl+C, Ctrl+Z)
  - Unicode and multiline input support
  - Built-in `multiWatch` command for monitoring multiple processes
- **Robust Error Handling**: Comprehensive error checking and graceful failure

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | Create new tab |
| `Ctrl+W` | Close current tab |
| `Ctrl+Tab` | Switch to next tab |
| `Ctrl+R` | Reverse history search |
| `Ctrl+C` | Interrupt foreground process |
| `Ctrl+Z` | Stop foreground process |
| `Ctrl+A` | Move cursor to beginning of line |
| `Ctrl+E` | Move cursor to end of line |
| `Tab` | Auto-complete files/directories |
| `ESC` | Exit terminal |

## Mouse Controls

- Click on tab headers to switch tabs
- Click in terminal area to focus input

## Compilation

```bash
gcc -o x11_window x11_window.c -lX11

```
### Requirements

- X11 development libraries
- GCC compiler
- Linux/Unix-like system

Usage


```bash
./x11_window

```

The terminal will open in an X11 window with one 

- initial tab. You can:
- Type commands and press Enter to execute
- Use Ctrl+N to create new tabs
- Click on tab headers or use Ctrl+Tab to switch tabs
- Use multiWatch to monitor multiple commands simultaneously

### Built-in Commands
- cd [directory] - Change directory
- history - Show command history
- multiWatch "cmd1" "cmd2" ... - Monitor multiple commands simultaneously

### MultiWatch Usage

```bash
multiWatch "ls -la" "ps aux" "whoami"
```

This will run all commands simultaneously and display their output with timestamps. Press Ctrl+C to stop monitoring.

### Error Handling
- Commands that don't exist show descriptive error messages
- Permission errors are handled gracefully
- Pipe and redirection errors are caught and reported
- Timeout protection for long-running commands
- Resource cleanup on exit

### License
- This project is for educational purposes as part of a terminal emulator implementation.
