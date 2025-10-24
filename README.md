# X11 Terminal Emulator with Tabs

A feature-rich terminal emulator built with **X11** that supports multiple tabs, command execution, pipes, redirection, and advanced features like **multiWatch** and **Unicode support**.

---

## 🚀 Features

- **Multi-tab Interface**: Create, switch, and close tabs with mouse or keyboard  
- **Command Execution**: Run shell commands with output capture  
- **Pipes and Redirection**: Support for `|`, `<`, `>` operators  
- **Advanced Features:**
  - Tab completion with file/directory suggestions  
  - Command history with reverse-i-search (Ctrl+R)  
  - Line editing (Ctrl+A, Ctrl+E, arrow keys)  
  - Signal handling (Ctrl+C, Ctrl+Z)  
  - Unicode and multiline input support  
  - Built-in `multiWatch` command for monitoring multiple processes  
  - Background job management (`jobs`, `fg` commands)  
  - Scrollback buffer with mouse wheel and keyboard navigation  
- **Robust Error Handling**: Comprehensive error checking and graceful failure  

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
|-----------|---------|
| Ctrl+N | Create new tab |
| Ctrl+W | Close current tab |
| Ctrl+Tab | Switch to next tab |
| Ctrl+R | Reverse history search |
| Ctrl+C | Interrupt foreground process |
| Ctrl+Z | Stop foreground process (send to background) |
| Ctrl+A | Move cursor to beginning of line |
| Ctrl+E | Move cursor to end of line |
| Tab | Auto-complete files/directories |
| Page Up/Down | Scroll through command output |
| Home/End | Scroll to top/bottom of buffer |
| ESC | Exit terminal |

---

## 🖱️ Mouse Controls

- Click on **tab headers** to switch tabs  
- Scroll with **mouse wheel** to navigate output  
- Click inside terminal to focus input  

---

## ⚙️ Compilation

```bash
gcc -o myterm your_file.c -lX11 -Wall -Wextra
```

### Requirements
- X11 development libraries  
- GCC compiler  
- Linux/Unix-like system  

---

## ▶️ Usage

```bash
./myterm
```
The terminal opens with one tab. You can:

- Type commands and press Enter to execute  
- Use **Ctrl+N** to create new tabs  
- Click or **Ctrl+Tab** to switch tabs  
- Use **multiWatch** to monitor multiple commands  
- Scroll through output with **Page Up/Down** or mouse wheel  

---

## 🧰 Built-in Commands

| Command | Description |
|----------|--------------|
| `cd [directory]` | Change directory |
| `history` | Show recent commands |
| `jobs` | List background jobs |
| `fg [job_id]` | Bring background job to foreground |
| `multiWatch "cmd1" "cmd2" ...` | Monitor multiple commands simultaneously |

---

## ⏱️ MultiWatch Usage

```bash
multiWatch "ls -la" "ps aux" "whoami"
```

Example output:
```
[12:30:45] MultiWatch [ls -la]:
----------------------------------------------------
(command output)
----------------------------------------------------
```
Press **Ctrl+C** to stop monitoring.

---

## 🔧 Background Jobs

- **Ctrl+Z** → Move running process to background  
- **jobs** → List background jobs  
- **fg [job_id]** → Bring job back to foreground  

---

## ✨ Auto-completion

- Press **Tab** to auto-complete file/directory names  
- Shows multiple matches if applicable  
- Expands to longest common prefix  

---

## 🔍 History Search

- Press **Ctrl+R** to enter reverse search mode  
- Type to find matching commands from history  
- Press Enter to execute selected command  

---

## 🛡️ Error Handling

- Unknown commands show descriptive errors  
- Permission and I/O errors handled gracefully  
- Pipes/redirection validated and safe  
- Timeout protection for long-running commands  
- Proper cleanup on exit  

---

## 🌏 Unicode Support

Supports Unicode and multilingual text:
```bash
echo "Hello in different languages:
नमस्ते (Hindi)
こんにちは (Japanese)
안녕하세요 (Korean)"
```

---

## 📚 Technical Notes

- Uses **X11** for GUI rendering  
- Proper **process management** with `fork()` and `execvp()`  
- Handles **SIGINT** and **SIGTSTP** signals  
- Supports **UTF-8 encoding**  
- Includes **scrollback buffer (1000 lines)**  
- Stores up to **10,000 history entries**  
- Cleans up resources safely on exit  

---

## 🧾 License

This project is for **educational purposes** as part of an academic terminal emulator implementation.
