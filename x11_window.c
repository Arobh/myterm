// ============================================================================
// INCLUDES
// ============================================================================

// X11 Graphics and Window System
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

// Standard C Library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <time.h>
#include <errno.h>

// Unicode and Wide Character Support
#include <wchar.h>
#include <wctype.h>

// POSIX System Calls
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

// Process and Signal Management
#include <signal.h>
#include <sys/wait.h>
#include <sys/poll.h>

// Debugging and Diagnostics
#include <execinfo.h>

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

// Display and Buffer Configuration
#define BUFFER_ROWS 25                    // Number of text rows in terminal
#define BUFFER_COLS 80                    // Number of text columns in terminal
#define CHAR_WIDTH 8                      // Character width in pixels
#define CHAR_HEIGHT 16                    // Character height in pixels

// Command and Input Configuration  
#define MAX_COMMAND_LENGTH 256            // Maximum command length
#define OUTPUT_BUFFER_SIZE 4096           // Output buffer size for command results
#define UTF8_BUFFER_SIZE (BUFFER_COLS * 4) // UTF-8 conversion buffer size

// History and Storage Configuration
#define MAX_HISTORY_SIZE 10000            // Maximum command history entries
#define SCROLLBACK_LINES 1000             // Scrollback buffer size

// Tab Management Configuration
#define MAX_TABS 10                       // Maximum number of tabs
#define MAX_TAB_NAME 32                   // Maximum tab name length

// MultiWatch Configuration (parallel command execution)
#define MAX_MULTIWATCH_COMMANDS 10        // Maximum simultaneous commands
#define MULTIWATCH_BUFFER_SIZE 1024       // MultiWatch output buffer size

// Background Jobs Configuration
#define MAX_BG_JOBS 100                   // Maximum background jobs

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * Background Process Structure
 * Tracks processes running in the background (via Ctrl+Z or &)
 */
typedef struct
{
    pid_t pid;                           // Process ID
    char status[16];                     // "Running" or "Stopped"
    char command[MAX_COMMAND_LENGTH];    // Original command string
    int job_id;                          // User-facing job ID
} BGProcess;

/**
 * MultiWatch Process Structure  
 * Tracks processes in multiWatch parallel execution mode
 */
typedef struct
{
    pid_t pid;                           // Process ID
    int fd;                              // File descriptor for output reading
    char command[MAX_COMMAND_LENGTH];    // Command being executed
    char temp_file[64];                  // Temporary file for output capture
    int active;                          // Whether process is still running
} MultiWatchProcess;

/**
 * Tab Structure
 * Represents a single terminal tab with complete state
 */
typedef struct
{
    // Display and Rendering
    wchar_t text_buffer[BUFFER_ROWS][BUFFER_COLS];  // Visible text buffer
    wchar_t scrollback_buffer[SCROLLBACK_LINES][BUFFER_COLS]; // History buffer
    int scrollback_count;                // Number of lines in scrollback
    int scrollback_offset;               // Current scroll position
    int max_scrollback_offset;           // Maximum scroll position reached
    
    // Command Input and Editing
    wchar_t current_command[MAX_COMMAND_LENGTH];    // Current command being typed
    int command_length;                  // Length of current command
    int cursor_row;                      // Cursor row position
    int cursor_col;                      // Cursor column position  
    int cursor_buffer_pos;               // Cursor position in command buffer
    
    // Command History
    wchar_t command_history[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH];
    int history_count;                   // Total history entries
    int history_current;                 // Current position in history navigation
    
    // Search Functionality
    int search_mode;                     // Whether in reverse search mode
    wchar_t search_buffer[MAX_COMMAND_LENGTH]; // Current search term
    int search_pos;                      // Search term length
    
    // Process Management
    pid_t foreground_pid;                // PID of foreground process (-1 if none)
    
    // Tab Identification
    char tab_name[MAX_TAB_NAME];         // Display name of tab
    int active;                          // Whether this tab is currently active
} Tab;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Tab Management
Tab tabs[MAX_TABS];                      // Array of all tabs
int tab_count = 1;                       // Number of active tabs
int active_tab_index = 0;                // Index of currently active tab

// Background Job Management
BGProcess bg_processes[MAX_BG_JOBS];     // Array of background processes
int job_counter = 0;                     // Counter for job ID assignment
int bg_job_count = 0;                    // Number of active background jobs

// MultiWatch Parallel Execution
MultiWatchProcess multiwatch_processes[MAX_MULTIWATCH_COMMANDS]; // MultiWatch processes
int multiwatch_count = 0;                // Number of active MultiWatch processes
int multiwatch_mode = 0;                 // Whether MultiWatch mode is active

// Signal Handling
volatile sig_atomic_t signal_received = 0;  // Flag indicating signal received
volatile sig_atomic_t which_signal = 0;     // Which specific signal was received

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Tab and buffer management
void initialize_text_buffer(void);
void initialize_tab(Tab *tab, const char *name);
void create_new_tab(void);
void close_current_tab(void);
void handle_tab_click(int click_x);

// Command execution and processing
void execute_command(Display *display, Window window, GC gc, Tab *tab, const char *command);
void handle_enter_key(Display *display, Window window, GC gc, Tab *tab);
void handle_keypress(Display *display, Window window, GC gc, XKeyEvent *key_event);
int is_safe_command(const char *command);

// History management
void add_to_history(Tab *tab, const char *command);
void handle_history_command(Tab *tab);
int search_history(Tab *tab, const wchar_t *search_term, wchar_t *result, int show_multiple);
void enter_search_mode(Tab *tab);
void handle_tab_completion(Tab *tab);

// Search and completion utilities
int find_longest_common_substring(const char *str1, const char *str2);
void debug_search(const char *search_term, const char *history_entry, int match_len);

// Display and rendering
void draw_text_buffer(Display *display, Window window, GC gc);
void update_command_display(Tab *tab);
void update_command_display_with_prompt(Tab *tab, const char *prompt);
void render_scrollback(Tab *tab);
void add_text_to_buffer(Tab *tab, const char *text);
void add_separator_line(Tab *tab);
void add_timestamp_line(Tab *tab);

// Scrollback and buffer management
void scroll_buffer(Tab *tab);
void scroll_up(Tab *tab);
void scroll_down(Tab *tab);
void scroll_to_bottom(Tab *tab);

// Background job management
void handle_jobs_command(Tab *tab);
void handle_fg_command(Tab *tab, const char *command);

// MultiWatch functionality
void handle_multiwatch_command(Display *display, Window window, GC gc, Tab *tab, const char *command);
void monitor_multiwatch_processes(Display *display, Window window, GC gc, Tab *tab);
void cleanup_multiwatch(void);

// Signal handlers
void handle_sigint(int sig);
void handle_sigtstp(int sig);
void handle_sigsegv(int sig);

// Resource cleanup
void cleanup_resources(Display *display, Window window, GC gc);
void cleanup_resources_default(void);

// X11 error handling
static int x11_error_handler(Display *display, XErrorEvent *error_event);


void render_scrollback(Tab *tab)
{
    // Safety check: return early if tab is null
    if (!tab)
        return;

    // Clear the entire visible text buffer with spaces
    for (int row = 0; row < BUFFER_ROWS; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            tab->text_buffer[row][col] = L' '; // Fill with space characters
        }
    }

    int total_scrollback_lines = tab->scrollback_count;
    int visible_content_lines = BUFFER_ROWS - 2; // Reserve 1 line for command prompt + 1 empty line at bottom

    // Calculate which scrollback lines to display based on current scroll offset
    int start_display_line = total_scrollback_lines - visible_content_lines - tab->scrollback_offset;

    // Ensure start line stays within valid bounds
    if (start_display_line < 0)
        start_display_line = 0;
    if (start_display_line > total_scrollback_lines - visible_content_lines)
    {
        start_display_line = total_scrollback_lines - visible_content_lines;
    }
    if (start_display_line < 0)
        start_display_line = 0;

    // Copy scrollback content to visible text buffer
    for (int visible_row = 0; visible_row < visible_content_lines; visible_row++)
    {
        int scrollback_line_index = start_display_line + visible_row;
        
        // Only copy if the scrollback line exists
        if (scrollback_line_index >= 0 && scrollback_line_index < total_scrollback_lines)
        {
            // Copy each character from scrollback buffer to visible buffer
            for (int col = 0; col < BUFFER_COLS; col++)
            {
                tab->text_buffer[visible_row][col] = tab->scrollback_buffer[scrollback_line_index][col];
            }
        }
    }

    // Always position command prompt at second-to-last row
    // (leaving the very bottom row empty for visual separation)
    tab->cursor_row = BUFFER_ROWS - 2;
    
    // Update the command prompt display
    update_command_display(tab);
}

void scroll_up(Tab *tab)
{
    // Calculate how many lines are actually visible for content
    // (subtracting 2 lines: 1 for command prompt, 1 for empty line at bottom)
    int visible_content_area = BUFFER_ROWS - 2;
    
    // If we don't have more content than can fit on screen, no scrolling needed
    if (tab->scrollback_count <= visible_content_area)
    {
        return; // Not enough content to scroll - everything fits on screen
    }

    // Calculate maximum possible scroll offset
    // This represents how far we can scroll before reaching the oldest content
    int max_scroll_offset = tab->scrollback_count - visible_content_area;
    if (max_scroll_offset < 0)
        max_scroll_offset = 0;

    // Only scroll if we haven't reached the maximum offset yet
    if (tab->scrollback_offset < max_scroll_offset)
    {
        // Increase scroll offset (moving view upward to see older content)
        tab->scrollback_offset++;
        
        // Update the maximum scroll offset reached (for tracking user's scroll position)
        if (tab->scrollback_offset > tab->max_scrollback_offset)
        {
            tab->max_scrollback_offset = tab->scrollback_offset;
        }
        
        // Re-render the scrollback with new scroll position
        render_scrollback(tab);
    }
    // If we're already at max offset, do nothing (can't scroll further up)
}

void scroll_down(Tab *tab)
{
    // Check if we're currently scrolled up (away from the most recent content)
    if (tab->scrollback_offset > 0)
    {
        // Scroll down toward more recent content by decreasing offset
        tab->scrollback_offset--;
        
        // Update the display with the new scroll position
        render_scrollback(tab);
    }
    else
    {
        // We're already at the bottom (most recent content)
        // Ensure offset is zero and refresh to show current state
        tab->scrollback_offset = 0;
        render_scrollback(tab);
    }
}

void scroll_to_bottom(Tab *tab)
{
    // Reset scroll offset to zero to show the most recent content
    // (scrollback_offset = 0 means we're at the bottom of the scrollback)
    tab->scrollback_offset = 0;
    
    // Refresh the display to show the current/bottom view
    render_scrollback(tab);
}

// Default cleanup if main doesn't run properly
void cleanup_resources_default(void)
{
    // Perform basic multiwatch cleanup (shared resources, etc.)
    cleanup_multiwatch();

    // Safely terminate any remaining child processes across all tabs
    for (int tab_index = 0; tab_index < tab_count; tab_index++)
    {
        // Check if this tab has an active foreground process
        if (tabs[tab_index].foreground_pid > 0)
        {
            // Forcefully terminate the process to prevent zombies
            kill(tabs[tab_index].foreground_pid, SIGKILL);
            
            // Note: We use SIGKILL instead of SIGTERM for immediate termination
            // since this is emergency cleanup and we want to ensure processes exit
        }
    }
    
    // Note: This function is designed as a safety net for when 
    // main() doesn't complete its normal cleanup sequence
}

void cleanup_resources(Display *display, Window window, GC gc)
{
    printf("Cleaning up resources...\n");

    // Step 1: Cleanup multiwatch system resources first
    cleanup_multiwatch();

    // Step 2: Cleanup background processes gracefully
    for (int bg_index = 0; bg_index < bg_job_count; bg_index++)
    {
        if (bg_processes[bg_index].pid > 0)
        {
            printf("Terminating background process %d\n", bg_processes[bg_index].pid);
            
            // First try graceful termination with SIGTERM
            kill(bg_processes[bg_index].pid, SIGTERM);
            
            // Give process a brief moment to clean up and exit gracefully
            usleep(50000); // 50ms delay
            
            // Check if process terminated, if not force kill with SIGKILL
            int process_status;
            if (waitpid(bg_processes[bg_index].pid, &process_status, WNOHANG) == 0)
            {
                // Process still running - force termination
                printf("Forcing termination of background process %d\n", bg_processes[bg_index].pid);
                kill(bg_processes[bg_index].pid, SIGKILL);
                waitpid(bg_processes[bg_index].pid, &process_status, 0);
            }
        }
    }
    // Reset background job counter
    bg_job_count = 0;

    // Step 3: Cleanup foreground processes in all tabs
    for (int tab_index = 0; tab_index < tab_count; tab_index++)
    {
        if (tabs[tab_index].foreground_pid > 0)
        {
            printf("Terminating process in tab %d: PID %d\n", tab_index, tabs[tab_index].foreground_pid);
            
            // Try graceful termination first
            kill(tabs[tab_index].foreground_pid, SIGTERM);
            
            // Brief delay to allow graceful shutdown
            usleep(50000); // 50ms
            
            // Check if process terminated gracefully
            int process_status;
            if (waitpid(tabs[tab_index].foreground_pid, &process_status, WNOHANG) == 0)
            {
                // Process still running - force kill and wait for completion
                printf("Forcing termination of tab %d process: PID %d\n", tab_index, tabs[tab_index].foreground_pid);
                kill(tabs[tab_index].foreground_pid, SIGKILL);
                waitpid(tabs[tab_index].foreground_pid, &process_status, 0);
            }
            
            // Mark process as terminated
            tabs[tab_index].foreground_pid = -1;
        }
    }

    // Step 4: Cleanup X11 resources in reverse creation order
    if (gc) {
        XFreeGC(display, gc);  // Free graphics context
        printf("Freed graphics context\n");
    }
    if (window) {
        XDestroyWindow(display, window);  // Destroy window
        printf("Destroyed window\n");
    }
    if (display) {
        XCloseDisplay(display);  // Close connection to X server
        printf("Closed display connection\n");
    }

    printf("Cleanup completed successfully.\n");
}

void handle_sigsegv(int sig)
{
    fprintf(stderr, "Segmentation fault occurred! (Signal: %d)\n", sig);
    fprintf(stderr, "=== DEBUG INFORMATION ===\n");
    
    // Print basic program state
    fprintf(stderr, "Active tab: %d, Total tabs: %d\n", active_tab_index, tab_count);

    // Only attempt to access tab data if the active tab index is valid
    // This prevents further segmentation faults while handling the original one
    if (active_tab_index >= 0 && active_tab_index < tab_count)
    {
        Tab *tab = &tabs[active_tab_index];
        fprintf(stderr, "Tab State:\n");
        fprintf(stderr, "  Search mode: %d, Search position: %d\n", tab->search_mode, tab->search_pos);
        fprintf(stderr, "  Command length: %d, History count: %d\n", tab->command_length, tab->history_count);

        // Safely print string buffers with length limits to prevent buffer overread
        fprintf(stderr, "  Search buffer: '%.*s'\n", 
                (int)sizeof(tab->search_buffer), tab->search_buffer);
        fprintf(stderr, "  Current command: '%.*s'\n", 
                (int)sizeof(tab->current_command), tab->current_command);
    }
    else
    {
        fprintf(stderr, "Warning: Active tab index is invalid or out of bounds\n");
    }

    // Generate and print stack trace for debugging
    fprintf(stderr, "\n=== STACK TRACE ===\n");
    void *call_stack[10];  // Array to store call stack addresses
    size_t stack_depth = backtrace(call_stack, 10);  // Capture up to 10 stack frames
    
    // Print human-readable stack trace to stderr
    backtrace_symbols_fd(call_stack, stack_depth, STDERR_FILENO);

    fprintf(stderr, "\n=== PROGRAM TERMINATING ===\n");
    
    // Exit with error code to indicate abnormal termination
    exit(EXIT_FAILURE);
}

// Safe command validation function
int is_safe_command(const char *command)
{
    // Safety check: reject null commands
    if (!command)
        return 0;

    // List of potentially dangerous command patterns to block
    // These patterns could lead to security issues or system damage
    const char *dangerous_patterns[] = {
        // Command chain separators that could execute multiple commands
        ";;",    // Sequential command execution
        "&&",    // Conditional AND execution  
        "||",    // Conditional OR execution
        
        // Command substitution patterns
        "`",     // Backtick command substitution
        "$(",    // Modern command substitution
        
        // Dangerous redirection targets
        "> /dev/",  // Redirecting to device files
        "> /proc/", // Redirecting to proc filesystem
        
        // Bypass attempts and privilege escalation
        "| tee",    // Could be used to write to protected files
        "> /etc/",  // Writing to system configuration
        ">> /etc/", // Appending to system configuration  
        "> /boot/", // Writing to boot files
        
        // Privilege escalation
        "sudo",     // Superuser command execution
        
        // Dangerous file permission changes
        "chmod 777", // Making files world-writable
        "chown root", // Changing ownership to root
        NULL        // Sentinel value to mark end of array
    };

    // Check command against each dangerous pattern
    for (int pattern_index = 0; dangerous_patterns[pattern_index] != NULL; pattern_index++)
    {
        if (strstr(command, dangerous_patterns[pattern_index]) != NULL)
        {
            // Dangerous pattern found - command is not safe
            return 0;
        }
    }

    // No dangerous patterns found - command appears to be safe
    return 1;
}

// Close the currently active tab and clean up its resources
void close_current_tab()
{
    // Prevent closing the last remaining tab to ensure user always has at least one tab
    if (tab_count <= 1)
    {
        printf("Cannot close the last tab\n");
        return;
    }

    // Safely terminate any running process in the tab being closed
    if (tabs[active_tab_index].foreground_pid > 0)
    {
        printf("Terminating process in tab %d (PID: %d)\n", 
               active_tab_index, tabs[active_tab_index].foreground_pid);
        
        // Send termination signal to the foreground process
        kill(tabs[active_tab_index].foreground_pid, SIGTERM);
        
        // Wait for the process to fully terminate to prevent zombie processes
        waitpid(tabs[active_tab_index].foreground_pid, NULL, 0);
    }

    // Shift all subsequent tabs left to fill the gap left by the closed tab
    for (int target_index = active_tab_index; target_index < tab_count - 1; target_index++)
    {
        tabs[target_index] = tabs[target_index + 1];
    }

    // Update tab count after removal
    tab_count--;

    // Adjust active tab index if we closed the last tab in the list
    if (active_tab_index >= tab_count)
    {
        active_tab_index = tab_count - 1;
    }

    // Mark the new active tab as active
    tabs[active_tab_index].active = 1;

    printf("Tab closed. Now %d tabs remaining. Active tab: %d\n", 
           tab_count, active_tab_index);
}

// Function to create a new tab with proper initialization
void create_new_tab()
{
    // Check if we haven't reached the maximum tab limit
    if (tab_count < MAX_TABS)
    {
        // Generate a default name for the new tab
        char tab_name[32];
        snprintf(tab_name, sizeof(tab_name), "Tab %d", tab_count + 1);

        // Get reference to the new tab in the tabs array
        Tab *new_tab = &tabs[tab_count];

        // Step 1: Initialize the tab structure with zeros to ensure clean state
        memset(new_tab, 0, sizeof(Tab));

        // Step 2: Set basic tab properties and state
        new_tab->cursor_buffer_pos = 0;      // Cursor at start of command line
        new_tab->command_length = 0;         // No command entered yet
        new_tab->cursor_row = BUFFER_ROWS - 1; // Cursor on bottom row (command line)
        new_tab->cursor_col = 2;             // Start after "> " prompt
        new_tab->foreground_pid = -1;        // No active process
        new_tab->history_count = 0;          // No command history yet
        new_tab->history_current = -1;       // Not browsing history
        new_tab->search_mode = 0;            // Search mode inactive
        new_tab->search_pos = 0;             // No search text entered
        new_tab->active = 0;                 // Not active yet (will be activated separately)

        // Initialize search buffer to empty
        memset(new_tab->search_buffer, 0, MAX_COMMAND_LENGTH);

        // Copy tab name with safety null-termination
        snprintf(new_tab->tab_name, MAX_TAB_NAME, "%s", tab_name);
        new_tab->tab_name[MAX_TAB_NAME - 1] = '\0'; // Ensure null termination

        // Step 3: Initialize the text buffer with spaces (clear screen)
        for (int row = 0; row < BUFFER_ROWS; row++)
        {
            for (int col = 0; col < BUFFER_COLS; col++)
            {
                new_tab->text_buffer[row][col] = ' ';
            }
        }

        // Step 4: Display welcome message and instructions
        char *welcome_message = "Welcome to X11 Shell Terminal!";
        char *instructions = "Type commands like 'ls' or 'pwd' and press ENTER";

        // Center the welcome message on row 1
        int welcome_len = strlen(welcome_message);
        int welcome_start = (BUFFER_COLS - welcome_len) / 2;
        for (int i = 0; i < welcome_len && welcome_start + i < BUFFER_COLS; i++)
        {
            new_tab->text_buffer[1][welcome_start + i] = welcome_message[i];
        }

        // Center the instructions on row 3
        int instr_len = strlen(instructions);
        int instr_start = (BUFFER_COLS - instr_len) / 2;
        for (int i = 0; i < instr_len && instr_start + i < BUFFER_COLS; i++)
        {
            new_tab->text_buffer[3][instr_start + i] = instructions[i];
        }

        // Step 5: Set up command prompt at bottom row
        new_tab->text_buffer[BUFFER_ROWS - 1][0] = '>';  // Prompt character
        new_tab->text_buffer[BUFFER_ROWS - 1][1] = ' ';  // Space after prompt

        // Step 6: Initialize current command buffer
        memset(new_tab->current_command, 0, MAX_COMMAND_LENGTH);

        // Successfully added the new tab
        tab_count++;
        
        printf("Created new tab: %s (Total tabs: %d)\n", tab_name, tab_count);
    }
    else
    {
        // Inform user when maximum tab limit is reached
        printf("Maximum tab limit (%d) reached. Cannot create new tab.\n", MAX_TABS);
    }
}

// Function to handle mouse clicks for tab switching
void handle_tab_click(int click_x)
{
    // Safety check: ensure we have valid tabs to work with
    if (tab_count <= 0 || tab_count > MAX_TABS)
        return;

    // Calculate the width of each tab in character columns
    int tab_width_chars = BUFFER_COLS / tab_count;
    
    // Ensure tab width is at least 1 character to avoid issues
    if (tab_width_chars < 1)
        tab_width_chars = 1;

    // Convert mouse X coordinate to character position, then determine which tab was clicked
    int character_position = click_x / CHAR_WIDTH;
    int clicked_tab_index = character_position / tab_width_chars;

    // Validate the calculated tab index is within bounds
    if (clicked_tab_index >= 0 && clicked_tab_index < tab_count)
    {
        // Deactivate the currently active tab if it exists and is valid
        if (active_tab_index >= 0 && active_tab_index < tab_count)
        {
            tabs[active_tab_index].active = 0;
        }

        // Activate the clicked tab
        active_tab_index = clicked_tab_index;
        tabs[active_tab_index].active = 1;

        printf("Switched to tab %d: %s\n", active_tab_index, tabs[active_tab_index].tab_name);
    }
    else
    {
        printf("Warning: Clicked tab index %d is out of bounds (0-%d)\n", 
               clicked_tab_index, tab_count - 1);
    }
}

// Function to scroll the entire buffer up by one line
void scroll_buffer(Tab *tab)
{
    // Safety check: ensure we have a valid tab pointer
    if (!tab)
        return;

    // Step 1: Scroll all lines upward by copying each line to the line above it
    // We stop at BUFFER_ROWS - 2 because we're copying from row+1 to row
    for (int row = 0; row < BUFFER_ROWS - 1; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            // Move content from the row below up to current row
            tab->text_buffer[row][col] = tab->text_buffer[row + 1][col];
        }
    }

    // Step 2: Clear the bottom row (now empty after scrolling) with spaces
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[BUFFER_ROWS - 1][col] = ' ';
    }

    // Step 3: Move cursor to the bottom row for new input
    tab->cursor_row = BUFFER_ROWS - 1;

    // Step 4: Ensure cursor column stays within valid bounds
    if (tab->cursor_col >= BUFFER_COLS)
    {
        tab->cursor_col = BUFFER_COLS - 1;
    }
}

// Function to add text to the buffer with proper encoding handling and scrollback management
void add_text_to_buffer(Tab *tab, const char *text)
{
    // Safety check: ensure valid tab and text input
    if (!tab || !text)
        return;

    // Step 1: Convert UTF-8 input to wide characters for display
    wchar_t wide_buffer[OUTPUT_BUFFER_SIZE];
    memset(wide_buffer, 0, sizeof(wide_buffer));

    size_t converted_chars = mbstowcs(wide_buffer, text, OUTPUT_BUFFER_SIZE - 1);
    
    // Handle UTF-8 conversion failure with ASCII fallback
    if (converted_chars == (size_t)-1)
    {
        // Conversion failed - fall back to simple ASCII character copying
        for (int i = 0; text[i] != '\0' && i < OUTPUT_BUFFER_SIZE - 1; i++)
        {
            wide_buffer[i] = (wchar_t)(unsigned char)text[i];
        }
        wide_buffer[OUTPUT_BUFFER_SIZE - 1] = L'\0'; // Ensure null termination
    }

    // Step 2: Process the text for scrollback buffer storage
    wchar_t scrollback_copy[OUTPUT_BUFFER_SIZE];
    wcscpy(scrollback_copy, wide_buffer); // Create working copy for line splitting

    wchar_t *current_line = scrollback_copy;
    wchar_t *newline_position;

    // Split the input text into individual lines based on newline characters
    do
    {
        // Find the next newline character in the current line
        newline_position = wcschr(current_line, L'\n');
        if (newline_position)
        {
            *newline_position = L'\0'; // Temporarily terminate at newline
        }

        // Add the current line to scrollback buffer
        if (tab->scrollback_count < SCROLLBACK_LINES)
        {
            // We have space - add to the end of scrollback buffer
            wcsncpy(tab->scrollback_buffer[tab->scrollback_count], current_line, BUFFER_COLS - 1);
            tab->scrollback_buffer[tab->scrollback_count][BUFFER_COLS - 1] = L'\0';
            tab->scrollback_count++;
        }
        else
        {
            // Scrollback buffer is full - shift all lines up to make space
            for (int i = 1; i < SCROLLBACK_LINES; i++)
            {
                wcscpy(tab->scrollback_buffer[i - 1], tab->scrollback_buffer[i]);
            }
            // Add new line at the bottom
            wcsncpy(tab->scrollback_buffer[SCROLLBACK_LINES - 1], current_line, BUFFER_COLS - 1);
            tab->scrollback_buffer[SCROLLBACK_LINES - 1][BUFFER_COLS - 1] = L'\0';
        }

        // Move to next line if we found a newline
        if (newline_position)
        {
            current_line = newline_position + 1;
        }
    } while (newline_position);

    // Step 3: Reset scroll position when new text is added
    // This ensures we're always viewing the most recent content by default
    tab->scrollback_offset = 0;
    tab->max_scrollback_offset = 0;

    // Step 4: Update the display to show the new content
    render_scrollback(tab);
}

// Helper function to add a visual separator line between command outputs
void add_separator_line(Tab *tab)
{
    // Safety check: ensure we have a valid tab
    if (!tab)
        return;

    // Step 1: Manage cursor position - make room for the separator line
    if (tab->cursor_row >= BUFFER_ROWS - 1)
    {
        // We're at the bottom of the buffer, scroll to make space
        scroll_buffer(tab);
    }
    else
    {
        // Move cursor to next line for the separator
        tab->cursor_row++;
    }
    
    // Reset cursor to the beginning of the line
    tab->cursor_col = 0;

    // Step 2: Create a separator line filled with dash characters
    wchar_t separator[BUFFER_COLS + 1]; // +1 for null terminator
    int separator_length = BUFFER_COLS - 1; // Leave one character margin
    
    // Fill the separator array with dash characters
    for (int i = 0; i < separator_length; i++)
    {
        separator[i] = L'-'; // Using wide character dashes
    }
    separator[separator_length] = L'\0'; // Null terminate the string

    // Step 3: Copy separator to the text buffer at current cursor row
    int chars_to_copy = wcslen(separator);
    
    // Ensure we don't exceed buffer boundaries
    if (chars_to_copy > BUFFER_COLS)
        chars_to_copy = BUFFER_COLS;

    // Copy separator characters to the text buffer
    for (int col = 0; col < chars_to_copy && tab->cursor_row < BUFFER_ROWS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = separator[col];
    }

    // Step 4: Fill the remaining part of the line with spaces
    // This ensures any previous content is properly cleared
    for (int col = chars_to_copy; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = L' ';
    }

    // Note: The cursor position is now at the start of the line after the separator
    // The next output will appear on the following line
}

// Helper function to add a timestamp line to mark command output timing
void add_timestamp_line(Tab *tab)
{
    // Safety check: ensure we have a valid tab
    if (!tab)
        return;

    // Step 1: Manage cursor position - make room for the timestamp line
    if (tab->cursor_row >= BUFFER_ROWS - 1)
    {
        // We're at the bottom of the buffer, scroll to make space
        scroll_buffer(tab);
    }
    else
    {
        // Move cursor to next line for the timestamp
        tab->cursor_row++;
    }
    
    // Reset cursor to the beginning of the line
    tab->cursor_col = 0;

    // Step 2: Generate formatted timestamp string
    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);
    char timestamp[64];
    
    // Format: "[HH:MM:SS] Output: "
    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] Output: ", time_info);

    // Step 3: Convert timestamp to wide characters for display
    wchar_t wide_timestamp[BUFFER_COLS];
    size_t converted_chars = mbstowcs(wide_timestamp, timestamp, BUFFER_COLS - 1);
    
    // Handle UTF-8 conversion failure with ASCII fallback
    if (converted_chars == (size_t)-1)
    {
        // Conversion failed - use simple character-by-character copying
        for (int i = 0; timestamp[i] != '\0' && i < BUFFER_COLS - 1; i++)
        {
            wide_timestamp[i] = (wchar_t)(unsigned char)timestamp[i];
        }
        wide_timestamp[BUFFER_COLS - 1] = L'\0'; // Ensure null termination
    }

    // Step 4: Copy timestamp to the text buffer
    int timestamp_length = wcslen(wide_timestamp);
    
    // Copy each character of the timestamp to the current row
    for (int col = 0; col < timestamp_length && col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = wide_timestamp[col];
    }

    // Step 5: Fill the remaining part of the line with spaces
    // This clears any previous content beyond the timestamp
    for (int col = timestamp_length; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = L' ';
    }

    // The cursor is now positioned at the start of this timestamp line
    // Next output will continue on this line after the timestamp text
}

void update_command_display(Tab *tab)
{
    // Position command prompt at second-to-last row, leaving the bottom row empty
    // This provides visual separation between the command input and previous output
    int command_row = BUFFER_ROWS - 2;

    // Step 1: Clear the entire command line to remove any previous content
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[command_row][col] = L' ';
    }

    // Step 2: Display the command prompt "> " at the beginning of the line
    tab->text_buffer[command_row][0] = L'>';  // Prompt character
    tab->text_buffer[command_row][1] = L' ';  // Space after prompt

    // Step 3: Display the current command text after the prompt
    int display_col = 2; // Start after "> " prompt
    
    for (int command_index = 0; 
         command_index < tab->command_length && display_col < BUFFER_COLS; 
         command_index++)
    {
        tab->text_buffer[command_row][display_col] = tab->current_command[command_index];
        display_col++;
    }

    // Step 4: Position the cursor appropriately within the command
    // Cursor position is offset by 2 to account for the "> " prompt
    tab->cursor_col = 2 + tab->cursor_buffer_pos;
    
    // Set cursor to the command row (second-to-last row)
    tab->cursor_row = command_row;
}

void handle_tab_completion(Tab *tab)
{
    // Step 1: Extract the current word being typed (from last space to cursor position)
    int word_start = tab->cursor_buffer_pos - 1;
    
    // Find the start of the current word by searching backwards for a space
    while (word_start >= 0 && tab->current_command[word_start] != L' ')
    {
        word_start--;
    }
    word_start++; // Move to the first character of the word (after the space)

    // Calculate the length of the word to complete
    int word_length = tab->cursor_buffer_pos - word_start;

    // If no word to complete, exit early
    if (word_length <= 0)
    {
        return;
    }

    // Step 2: Convert wide character word to multibyte for filesystem operations
    char current_word[MAX_COMMAND_LENGTH];
    size_t converted = wcstombs(current_word, &tab->current_command[word_start], word_length);
    
    // Handle conversion failure with ASCII fallback
    if (converted == (size_t)-1)
    {
        for (int i = 0; i < word_length && i < MAX_COMMAND_LENGTH - 1; i++)
        {
            current_word[i] = (char)tab->current_command[word_start + i];
        }
    }
    current_word[word_length] = '\0'; // Ensure null termination

    // Step 3: Scan current directory for filename matches
    DIR *directory = opendir(".");
    if (directory == NULL)
    {
        return; // Cannot open directory
    }

    struct dirent *directory_entry;
    char matches[256][MAX_COMMAND_LENGTH]; // Store matching filenames
    int match_count = 0;
    char common_prefix[MAX_COMMAND_LENGTH] = ""; // Longest common prefix of all matches

    // Iterate through directory entries to find matches
    while ((directory_entry = readdir(directory)) != NULL && match_count < 256)
    {
        char *filename = directory_entry->d_name;

        // Skip hidden files unless the user explicitly typed a dot
        if (filename[0] == '.' && current_word[0] != '.')
        {
            continue;
        }

        // Check if filename starts with the current word (case-sensitive match)
        if (strncmp(filename, current_word, word_length) == 0)
        {
            // Safe copy of matching filename
            if (snprintf(matches[match_count], MAX_COMMAND_LENGTH, "%s", filename) < MAX_COMMAND_LENGTH)
            {
                // Update common prefix with this new match
                if (match_count == 0)
                {
                    // First match - initialize common prefix
                    snprintf(common_prefix, MAX_COMMAND_LENGTH, "%s", filename);
                }
                else
                {
                    // Find longest common prefix between current prefix and new match
                    int prefix_length;
                    for (prefix_length = 0; 
                         common_prefix[prefix_length] != '\0' && 
                         matches[match_count][prefix_length] != '\0'; 
                         prefix_length++)
                    {
                        if (common_prefix[prefix_length] != matches[match_count][prefix_length])
                        {
                            break;
                        }
                    }
                    common_prefix[prefix_length] = '\0'; // Truncate to common portion
                }
                match_count++;
            }
        }
    }

    closedir(directory);

    // Step 4: Handle the completion results
    if (match_count == 0)
    {
        // No matching files found - do nothing
        return;
    }
    else if (match_count == 1)
    {
        // Single match found - complete the word automatically
        char *completion = matches[0];
        int completion_length = strlen(completion);

        // Replace current word with the complete filename
        for (int i = 0; i < completion_length; i++)
        {
            if (word_start + i < MAX_COMMAND_LENGTH - 1)
            {
                tab->current_command[word_start + i] = (wchar_t)completion[i];
            }
        }

        // Update command buffer state
        int new_command_length = word_start + completion_length;
        if (new_command_length < MAX_COMMAND_LENGTH)
        {
            tab->current_command[new_command_length] = L'\0';
            tab->command_length = new_command_length;
            tab->cursor_buffer_pos = new_command_length;
        }

        // Add trailing space if we're not at the end of the line
        if (tab->cursor_buffer_pos < MAX_COMMAND_LENGTH - 1 && 
            tab->current_command[tab->cursor_buffer_pos] == L'\0')
        {
            tab->current_command[tab->cursor_buffer_pos] = L' ';
            tab->command_length++;
            tab->current_command[tab->command_length] = L'\0';
            tab->cursor_buffer_pos++;
        }
    }
    else
    {
        // Multiple matches found
        if (strlen(common_prefix) > word_length)
        {
            // Complete to the longest common prefix of all matches
            char *completion = common_prefix;
            int completion_length = strlen(completion);

            // Replace current word with common prefix
            for (int i = 0; i < completion_length; i++)
            {
                if (word_start + i < MAX_COMMAND_LENGTH - 1)
                {
                    tab->current_command[word_start + i] = (wchar_t)completion[i];
                }
            }

            // Update command buffer state
            int new_command_length = word_start + completion_length;
            if (new_command_length < MAX_COMMAND_LENGTH)
            {
                tab->current_command[new_command_length] = L'\0';
                tab->command_length = new_command_length;
                tab->cursor_buffer_pos = new_command_length;
            }
        }

        // Step 5: Display all available matches to the user
        printf("\n"); // New line in terminal
        add_text_to_buffer(tab, ""); // Add blank line in X11 display

        // Format matches in columns to fit within buffer width
        char formatted_line[BUFFER_COLS] = {0};
        size_t current_line_length = 0;

        for (int match_index = 0; match_index < match_count; match_index++)
        {
            char *filename = matches[match_index];
            int filename_length = strlen(filename);
            
            // Calculate space needed: filename + 2 spaces for separation
            int space_needed = filename_length + 2;

            // If this match won't fit on current line, output current line and start new one
            if (current_line_length + space_needed >= BUFFER_COLS)
            {
                add_text_to_buffer(tab, formatted_line);
                formatted_line[0] = '\0';
                current_line_length = 0;
            }

            // Add separator if not the first item on the line
            if (current_line_length > 0)
            {
                snprintf(formatted_line + current_line_length, 
                        sizeof(formatted_line) - current_line_length, "  ");
                current_line_length += 2;
            }

            // Add the filename to the current line
            snprintf(formatted_line + current_line_length, 
                    sizeof(formatted_line) - current_line_length, "%s", filename);
            current_line_length += filename_length;
        }

        // Output any remaining matches on the last line
        if (current_line_length > 0)
        {
            add_text_to_buffer(tab, formatted_line);
        }

        // Add blank line after matches for visual separation
        add_text_to_buffer(tab, "");
    }

    // Step 6: Update the command display to show changes
    update_command_display(tab);
}

// Function to add a command to the command history
void add_to_history(Tab *tab, const char *command)
{
    // Skip empty commands to avoid cluttering history
    if (strlen(command) == 0)
        return;

    // Step 1: Convert multibyte command to wide characters for consistent storage
    wchar_t wide_command[MAX_COMMAND_LENGTH];
    size_t converted_chars = mbstowcs(wide_command, command, MAX_COMMAND_LENGTH - 1);
    
    // Handle UTF-8 conversion failure with ASCII fallback
    if (converted_chars == (size_t)-1)
    {
        // Fallback: convert character by character
        for (int i = 0; command[i] != '\0' && i < MAX_COMMAND_LENGTH - 1; i++)
        {
            wide_command[i] = (wchar_t)(unsigned char)command[i];
        }
        wide_command[strlen(command)] = L'\0'; // Ensure null termination
    }

    // Step 2: Check for duplicate commands (don't add consecutive duplicates)
    if (tab->history_count > 0)
    {
        // Compare with the most recent history entry
        int last_history_index = tab->history_count - 1;
        if (wcscmp(tab->command_history[last_history_index], wide_command) == 0)
        {
            return; // Skip if this command is same as the previous one
        }
    }

    // Step 3: Add command to history storage
    if (tab->history_count < MAX_HISTORY_SIZE)
    {
        // History has space - add to the end
        wcscpy(tab->command_history[tab->history_count], wide_command);
        tab->history_count++;
    }
    else
    {
        // History is full - remove oldest entry and shift others up
        for (int i = 1; i < MAX_HISTORY_SIZE; i++)
        {
            wcscpy(tab->command_history[i - 1], tab->command_history[i]);
        }
        // Add new command to the end (now available after shift)
        wcscpy(tab->command_history[MAX_HISTORY_SIZE - 1], wide_command);
        // Note: history_count remains MAX_HISTORY_SIZE since we're at capacity
    }

    // Step 4: Reset history navigation to the end (most recent command)
    tab->history_current = tab->history_count;
    
    // Note: history_current points to one position past the last valid entry
    // This allows easy navigation when using up/down arrows
}

// Simple and reliable longest common substring function
int find_longest_common_substring(const char *str1, const char *str2)
{
    // Step 1: Validate input strings
    if (!str1 || !str2 || str1[0] == '\0' || str2[0] == '\0')
    {
        return 0; // Invalid or empty strings have no common substring
    }

    int str1_length = strlen(str1);
    int str2_length = strlen(str2);
    int max_common_length = 0;

    printf("DEBUG LCS: Comparing '%s' (length: %d) and '%s' (length: %d)\n", 
           str1, str1_length, str2, str2_length);

    // Step 2: Check for exact substring match (case-sensitive)
    // If str1 is a complete substring of str2, return str1's length
    if (strstr(str2, str1) != NULL)
    {
        printf("DEBUG LCS: Found exact substring match: %d\n", str1_length);
        return str1_length;
    }

    // Step 3: Check for case-insensitive substring match
    // Create lowercase copies for case-insensitive comparison
    char str1_lower[1024];
    char str2_lower[1024];
    
    // Safely copy and convert str1 to lowercase
    strncpy(str1_lower, str1, sizeof(str1_lower) - 1);
    str1_lower[sizeof(str1_lower) - 1] = '\0';
    for (int i = 0; str1_lower[i]; i++)
    {
        str1_lower[i] = tolower(str1_lower[i]);
    }

    // Safely copy and convert str2 to lowercase
    strncpy(str2_lower, str2, sizeof(str2_lower) - 1);
    str2_lower[sizeof(str2_lower) - 1] = '\0';
    for (int i = 0; str2_lower[i]; i++)
    {
        str2_lower[i] = tolower(str2_lower[i]);
    }

    // Check if lowercase str1 is a substring of lowercase str2
    if (strstr(str2_lower, str1_lower) != NULL)
    {
        printf("DEBUG LCS: Found case-insensitive substring match: %d\n", str1_length);
        return str1_length;
    }

    // Step 4: Use dynamic programming approach to find longest common substring
    // This handles partial matches where strings share common segments
    for (int str1_index = 0; str1_index < str1_length; str1_index++)
    {
        for (int str2_index = 0; str2_index < str2_length; str2_index++)
        {
            int current_match_length = 0;
            
            // Compare characters from current positions in both strings
            while (str1_index + current_match_length < str1_length && 
                   str2_index + current_match_length < str2_length &&
                   tolower(str1[str1_index + current_match_length]) == 
                   tolower(str2[str2_index + current_match_length]))
            {
                current_match_length++;
            }
            
            // Update maximum length if we found a longer match
            if (current_match_length > max_common_length)
            {
                max_common_length = current_match_length;
                printf("DEBUG LCS: New max_common_length=%d at str1_index=%d, str2_index=%d\n", 
                       max_common_length, str1_index, str2_index);
            }
        }
    }

    printf("DEBUG LCS: Final max_common_length=%d\n", max_common_length);
    return max_common_length;
}

// Enter reverse-i-search mode for command history searching
void enter_search_mode(Tab *tab)
{
    // Safety check: ensure we have a valid tab
    if (!tab)
        return;

    // Step 1: Activate search mode and initialize search state
    tab->search_mode = 1;        // Enable search mode flag
    tab->search_pos = 0;         // Start with empty search string
    memset(tab->search_buffer, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t)); // Clear search buffer

    // Step 2: Clear the current command line to prepare for search interface
    // This ensures the command line shows search prompts instead of regular commands
    memset(tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
    tab->command_length = 0;     // Reset command length
    tab->cursor_buffer_pos = 0;  // Reset cursor position

    // Step 3: Update display to show search prompt and mode
    // The prompt "(reverse-i-search)`': " indicates we're in reverse incremental search mode
    // The backticks will contain the search string as the user types
    update_command_display_with_prompt(tab, "(reverse-i-search)`': ");
}

// Debug function to trace search matching behavior
// This helps diagnose issues with reverse-i-search functionality
void debug_search(const char *search_term, const char *history_entry, int match_len)
{
    printf("DEBUG SEARCH: search_term='%s', history_entry='%s', match_length=%d\n",
           search_term, history_entry, match_len);
}

// Enhanced search_history function with proper multiple match display
int search_history(Tab *tab, const wchar_t *search_term, wchar_t *result, int show_multiple)
{
    // Step 1: Validate input parameters
    if (!tab || !search_term || !result || wcslen(search_term) == 0)
    {
        return 0; // Invalid input or empty search term
    }
    
    // Ensure search term doesn't exceed buffer limits
    if (wcslen(search_term) >= MAX_COMMAND_LENGTH)
    {
        return 0;
    }

    // Step 2: Convert wide character search term to multibyte for string comparison
    char multibyte_search[MAX_COMMAND_LENGTH * 4] = {0};
    size_t converted_chars = wcstombs(multibyte_search, search_term, sizeof(multibyte_search) - 1);
    
    // Handle conversion failure with ASCII fallback
    if (converted_chars == (size_t)-1)
    {
        for (int i = 0; i < MAX_COMMAND_LENGTH - 1 && search_term[i] != L'\0'; i++)
        {
            multibyte_search[i] = (char)search_term[i];
        }
        multibyte_search[wcslen(search_term)] = '\0';
    }

    // Constants for match management
    #define MAX_DISPLAY_MATCHES 10

    // Structure to store matching history entries with metadata
    typedef struct
    {
        wchar_t command[MAX_COMMAND_LENGTH]; // The matching command text
        int match_length;                    // Length of the common substring match
        int history_index;                   // Original position in history array
    } HistoryMatch;

    HistoryMatch matches[MAX_DISPLAY_MATCHES] = {0};
    int match_count = 0;

    // Step 3: Search through command history from most recent to oldest
    for (int history_index = tab->history_count - 1; history_index >= 0; history_index--)
    {
        // Skip empty history entries
        if (tab->command_history[history_index][0] == L'\0')
            continue;

        // Convert history entry to multibyte for substring comparison
        char multibyte_history[MAX_COMMAND_LENGTH * 4] = {0};
        converted_chars = wcstombs(multibyte_history, tab->command_history[history_index], 
                                 sizeof(multibyte_history) - 1);
        if (converted_chars == (size_t)-1)
        {
            // Fallback conversion for non-UTF-8 strings
            for (int j = 0; j < MAX_COMMAND_LENGTH - 1 && tab->command_history[history_index][j] != L'\0'; j++)
            {
                multibyte_history[j] = (char)tab->command_history[history_index][j];
            }
            multibyte_history[wcslen(tab->command_history[history_index])] = '\0';
        }

        // Calculate match quality using longest common substring
        int match_length = find_longest_common_substring(multibyte_search, multibyte_history);

        // Store match if it meets minimum criteria and we have space
        if (match_length >= 1 && match_count < MAX_DISPLAY_MATCHES)
        {
            matches[match_count].match_length = match_length;
            matches[match_count].history_index = history_index;
            wcsncpy(matches[match_count].command, tab->command_history[history_index], MAX_COMMAND_LENGTH - 1);
            matches[match_count].command[MAX_COMMAND_LENGTH - 1] = L'\0';
            match_count++;
        }
    }

    // Step 4: Handle no matches found
    if (match_count == 0)
    {
        return show_multiple ? -1 : 0; // Return -1 for "no matches" in show_multiple mode
    }

    // Step 5: Display multiple matches to user if requested and available
    if (show_multiple && match_count > 1)
    {
        add_text_to_buffer(tab, ""); // Add blank line for visual separation
        add_text_to_buffer(tab, "Multiple matches found:");

        // Display each matching command with numbering
        for (int match_index = 0; match_index < match_count; match_index++)
        {
            char display_line[256];
            char multibyte_command[MAX_COMMAND_LENGTH * 4] = {0};

            // Convert matching command to multibyte for display
            wcstombs(multibyte_command, matches[match_index].command, sizeof(multibyte_command) - 1);
            multibyte_command[sizeof(multibyte_command) - 1] = '\0';

            // Format: "  1: ls -la", "  2: ls -l", etc.
            snprintf(display_line, sizeof(display_line), "  %d: %s", match_index + 1, multibyte_command);
            add_text_to_buffer(tab, display_line);
        }

        add_text_to_buffer(tab, "Press number to select or refine search");
        return match_count; // Return count of matches for selection handling
    }

    // Step 6: Return the single best match (longest common substring)
    if (match_count > 0)
    {
        // Find the match with the longest common substring
        int best_match_index = 0;
        for (int match_index = 1; match_index < match_count; match_index++)
        {
            if (matches[match_index].match_length > matches[best_match_index].match_length)
            {
                best_match_index = match_index;
            }
        }
        
        // Copy the best matching command to the result buffer
        wcsncpy(result, matches[best_match_index].command, MAX_COMMAND_LENGTH - 1);
        result[MAX_COMMAND_LENGTH - 1] = L'\0';
        return 1; // Success: single match returned
    }

    return 0; // No matches found (fallback)
}

void handle_history_command(Tab *tab)
{
    // Step 1: Check if there is any command history to display
    if (tab->history_count == 0)
    {
        add_text_to_buffer(tab, "No command history");
        return;
    }

    // Step 2: Calculate display range - show last 10 commands or all if less than 10
    int start_index = (tab->history_count > 10) ? tab->history_count - 10 : 0;
    int commands_to_display = tab->history_count - start_index;

    // Optional: Show header with total count
    char header[64];
    snprintf(header, sizeof(header), "Command history (%d commands, showing last %d):", 
             tab->history_count, commands_to_display);
    add_text_to_buffer(tab, header);

    // Step 3: Display each history entry with numbering
    for (int history_index = start_index; history_index < tab->history_count; history_index++)
    {
        char formatted_line[256];
        char multibyte_command[MAX_COMMAND_LENGTH * 4] = {0};

        // Step 4: Convert wide character command to multibyte for display
        size_t converted_chars = wcstombs(multibyte_command, 
                                        tab->command_history[history_index], 
                                        sizeof(multibyte_command) - 1);
        
        // Handle UTF-8 conversion failure with ASCII fallback
        if (converted_chars == (size_t)-1)
        {
            // Fallback: convert each wide character individually
            int char_index;
            for (char_index = 0; 
                 char_index < MAX_COMMAND_LENGTH - 1 && 
                 tab->command_history[history_index][char_index] != L'\0'; 
                 char_index++)
            {
                multibyte_command[char_index] = (char)tab->command_history[history_index][char_index];
            }
            multibyte_command[char_index] = '\0'; // Ensure null termination
        }
        else
        {
            multibyte_command[converted_chars] = '\0'; // Ensure null termination
        }

        // Step 5: Format and display the history entry
        // Format: "  1: ls -la", "  2: cd /home/user", etc.
        // Limit display length to prevent buffer overflow and ensure readability
        snprintf(formatted_line, sizeof(formatted_line), "  %d: %.200s", 
                 history_index + 1,  // Show 1-based numbering for user-friendly display
                 multibyte_command);
        
        add_text_to_buffer(tab, formatted_line);
    }

    // Optional: Add footer with usage hint
    add_text_to_buffer(tab, "Use up/down arrows to navigate history during command entry");
}

void update_command_display_with_prompt(Tab *tab, const char *prompt)
{
    // Safety check: ensure valid tab and prompt
    if (!tab || !prompt)
        return;

    // Step 1: Clear the current command line completely
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = L' ';
    }

    // Step 2: Convert the prompt text to wide characters for display
    wchar_t wide_prompt[BUFFER_COLS] = {0};
    size_t converted_chars = mbstowcs(wide_prompt, prompt, BUFFER_COLS - 1);
    
    // Handle UTF-8 conversion failure with ASCII fallback
    if (converted_chars == (size_t)-1)
    {
        // Fallback: convert each character individually
        for (int char_index = 0; prompt[char_index] != '\0' && char_index < BUFFER_COLS - 1; char_index++)
        {
            wide_prompt[char_index] = (wchar_t)(unsigned char)prompt[char_index];
        }
        wide_prompt[BUFFER_COLS - 1] = L'\0'; // Ensure null termination
    }

    // Step 3: Display the prompt text at the beginning of the line
    int prompt_length = wcslen(wide_prompt);
    for (int col = 0; col < prompt_length && col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = wide_prompt[col];
    }

    // Step 4: Display the current search buffer content after the prompt
    int display_column = prompt_length; // Start position for search text
    
    for (int search_index = 0; 
         search_index < tab->search_pos && display_column < BUFFER_COLS; 
         search_index++)
    {
        tab->text_buffer[tab->cursor_row][display_column] = tab->search_buffer[search_index];
        display_column++;
    }

    // Step 5: Position the cursor appropriately
    // Cursor goes after both the prompt and the current search text
    tab->cursor_col = prompt_length + tab->search_pos;
    
    // Ensure cursor stays within buffer bounds
    if (tab->cursor_col >= BUFFER_COLS)
    {
        tab->cursor_col = BUFFER_COLS - 1;
    }
    
    // Note: cursor_row remains unchanged - we're updating the existing command line
}

// Function to initialize a tab's text buffer and state
void initialize_tab(Tab *tab, const char *name)
{
    // Step 1: Initialize command input state
    tab->cursor_buffer_pos = 0;      // Cursor at start of command buffer
    tab->command_length = 0;         // No command entered yet
    tab->cursor_row = BUFFER_ROWS - 1; // Cursor on bottom row
    tab->cursor_col = 2;             // Start after "> " prompt
    tab->foreground_pid = -1;        // No active process
    tab->history_count = 0;          // Empty command history
    tab->history_current = -1;       // Not browsing history
    tab->search_mode = 0;            // Search mode inactive
    tab->search_pos = 0;             // No search text entered

    // Initialize search buffer with zeros
    memset(tab->search_buffer, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));

    // Set tab name with safe string copying
    snprintf(tab->tab_name, MAX_TAB_NAME, "%s", name);
    tab->tab_name[MAX_TAB_NAME - 1] = '\0'; // Ensure null termination

    // Step 2: Initialize scrollback buffer system
    tab->scrollback_count = 0;       // No scrollback content yet
    tab->scrollback_offset = 0;      // Viewing most recent content
    tab->max_scrollback_offset = 0;  // No scroll history yet

    // Clear scrollback buffer with spaces
    for (int line_index = 0; line_index < SCROLLBACK_LINES; line_index++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            tab->scrollback_buffer[line_index][col] = L' ';
        }
    }

    // Step 3: Initialize visible text buffer with spaces (clear screen)
    for (int row = 0; row < BUFFER_ROWS; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            tab->text_buffer[row][col] = L' ';
        }
    }

    // Step 4: Display welcome message and instructions
    wchar_t *welcome_message = L"Welcome to X11 Shell Terminal!";
    wchar_t *instructions = L"Type commands like 'ls' or 'pwd' and press ENTER";

    // Center welcome message on row 2
    int welcome_length = wcslen(welcome_message);
    int welcome_start_col = (BUFFER_COLS - welcome_length) / 2;
    for (int i = 0; i < welcome_length && welcome_start_col + i < BUFFER_COLS; i++)
    {
        tab->text_buffer[2][welcome_start_col + i] = welcome_message[i];
    }

    // Center instructions on row 4
    int instructions_length = wcslen(instructions);
    int instructions_start_col = (BUFFER_COLS - instructions_length) / 2;
    for (int i = 0; i < instructions_length && instructions_start_col + i < BUFFER_COLS; i++)
    {
        tab->text_buffer[4][instructions_start_col + i] = instructions[i];
    }

    // Step 5: Set up command prompt at second-to-last row
    // This leaves the bottom row empty for visual separation
    tab->text_buffer[BUFFER_ROWS - 2][0] = L'>';  // Prompt character
    tab->text_buffer[BUFFER_ROWS - 2][1] = L' ';  // Space after prompt

    // Step 6: Initialize current command buffer
    memset(tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
}

// Initialize the text buffer system and create the first default tab
void initialize_text_buffer()
{
    // Step 1: Clear all tabs to ensure clean initial state
    // This prevents any garbage data from previous memory usage
    memset(tabs, 0, sizeof(tabs));

    // Step 2: Initialize the first tab with default name
    initialize_tab(&tabs[0], "Tab 1");
    
    // Step 3: Set the first tab as active
    tabs[0].active = 1;           // Mark this tab as currently active
    tab_count = 1;                // We now have one tab
    active_tab_index = 0;         // First tab (index 0) is active

    // Step 4: Validate initial state for safety
    // This ensures our active tab index is always valid, even if initialization logic changes
    if (active_tab_index >= tab_count)
    {
        active_tab_index = 0;     // Reset to first tab if invalid
        printf("Warning: Corrected invalid active_tab_index during initialization\n");
    }

    printf("Initialized text buffer system with %d tab(s). Active tab: %d\n", 
           tab_count, active_tab_index);
}

// Function to draw the entire text buffer to the X11 window
void draw_text_buffer(Display *display, Window window, GC gc)
{
    // Clear the entire window to start with a fresh drawing surface
    XClearWindow(display, window);

    // Step 1: Safety checks for tab system state
    if (tab_count <= 0 || tab_count > MAX_TABS)
        return;
    if (active_tab_index < 0 || active_tab_index >= tab_count)
        return;

    Tab *active_tab = &tabs[active_tab_index];

    // Step 2: Draw scrollback position indicator if user has scrolled up
    if (active_tab->scrollback_offset > 0)
    {
        char scroll_indicator[64];
        int total_scrollback_lines = active_tab->scrollback_count;
        int visible_content_lines = BUFFER_ROWS - 1; // Reserve bottom row for command line
        int current_scroll_position = total_scrollback_lines - visible_content_lines - active_tab->scrollback_offset;

        // Calculate scroll position as percentage for user feedback
        int scroll_percentage = 0;
        if (total_scrollback_lines > visible_content_lines)
        {
            scroll_percentage = (current_scroll_position * 100) / (total_scrollback_lines - visible_content_lines);
            // Clamp percentage to valid range
            if (scroll_percentage < 0)
                scroll_percentage = 0;
            if (scroll_percentage > 100)
                scroll_percentage = 100;
        }

        snprintf(scroll_indicator, sizeof(scroll_indicator), "Scroll: %d%% (%d/%d lines)",
                 scroll_percentage, current_scroll_position, total_scrollback_lines);

        XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));
        XDrawString(display, window, gc, 10, 15, scroll_indicator, strlen(scroll_indicator));
    }

    // Step 3: Draw tab headers at the top of the window
    int tab_width_chars = BUFFER_COLS / tab_count;
    if (tab_width_chars < 1)
        tab_width_chars = 1;

    for (int tab_index = 0; tab_index < tab_count && tab_index < MAX_TABS; tab_index++)
    {
        int tab_start_x = tab_index * tab_width_chars;

        // Skip drawing if tab position is outside valid bounds
        if (tab_start_x < 0 || tab_start_x >= BUFFER_COLS)
            continue;

        // Set colors based on whether this tab is active
        if (tab_index == active_tab_index)
        {
            // Active tab: black background with white text
            XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));
            XFillRectangle(display, window, gc, tab_start_x * CHAR_WIDTH, 0,
                           tab_width_chars * CHAR_WIDTH, CHAR_HEIGHT);
            XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
        }
        else
        {
            // Inactive tab: white background with black text
            XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
            XFillRectangle(display, window, gc, tab_start_x * CHAR_WIDTH, 0,
                           tab_width_chars * CHAR_WIDTH, CHAR_HEIGHT);
            XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));
        }

        // Prepare tab name for display with truncation if needed
        char display_name[MAX_TAB_NAME];
        int max_display_chars = (tab_width_chars - 2) > 0 ? (tab_width_chars - 2) : 1;
        if (max_display_chars > MAX_TAB_NAME - 1)
            max_display_chars = MAX_TAB_NAME - 1;

        snprintf(display_name, max_display_chars + 1, "%s", tabs[tab_index].tab_name);
        display_name[max_display_chars] = '\0';

        // Draw the tab name centered within the tab header
        XDrawString(display, window, gc,
                    (tab_start_x + 1) * CHAR_WIDTH, CHAR_HEIGHT - 2,
                    display_name, strlen(display_name));
    }

    // Step 4: Draw the text content of the active tab
    XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));

    // Draw each character in the text buffer (excluding the bottom row for visual separation)
    for (int row = 0; row < BUFFER_ROWS - 1; row++) // Stop before bottom row
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            // Only draw non-space characters to improve performance
            if (active_tab->text_buffer[row][col] != L' ')
            {
                int pixel_x = col * CHAR_WIDTH;
                int pixel_y = (row + 1) * CHAR_HEIGHT; // +1 to account for tab header row

                // Ensure drawing coordinates are within window bounds
                if (pixel_x >= 0 && pixel_x < BUFFER_COLS * CHAR_WIDTH &&
                    pixel_y >= CHAR_HEIGHT && pixel_y < BUFFER_ROWS * CHAR_HEIGHT)
                {
                    // Convert wide character to multibyte for X11 drawing
                    char multibyte_char[MB_CUR_MAX + 1];
                    int char_length = wctomb(multibyte_char, active_tab->text_buffer[row][col]);
                    if (char_length > 0)
                    {
                        multibyte_char[char_length] = '\0';
                        XDrawString(display, window, gc, pixel_x, pixel_y, multibyte_char, char_length);
                    }
                    else
                    {
                        // Fallback for invalid characters - display question mark
                        char fallback_char[2] = {'?', '\0'};
                        XDrawString(display, window, gc, pixel_x, pixel_y, fallback_char, 1);
                    }
                }
            }
        }
    }

    // Step 5: Draw the text cursor at current position
    int cursor_pixel_x = active_tab->cursor_col * CHAR_WIDTH;
    int cursor_pixel_y = (active_tab->cursor_row + 1) * CHAR_HEIGHT + 1; // +1 for tab header, +1 for vertical offset

    // Ensure cursor is drawn within window bounds
    if (cursor_pixel_x >= 0 && cursor_pixel_x < BUFFER_COLS * CHAR_WIDTH &&
        cursor_pixel_y >= CHAR_HEIGHT && cursor_pixel_y < (BUFFER_ROWS + 1) * CHAR_HEIGHT)
    {
        XDrawString(display, window, gc, cursor_pixel_x, cursor_pixel_y, "_", 1);
    }
}

// Function to cleanup multiWatch processes and temporary files
void cleanup_multiwatch()
{
    printf("Cleaning up multiWatch processes and resources\n");

    // Iterate through all registered multiwatch processes
    for (int process_index = 0; process_index < multiwatch_count; process_index++)
    {
        if (multiwatch_processes[process_index].active)
        {
            int process_pid = multiwatch_processes[process_index].pid;
            printf("Terminating multiwatch process %d\n", process_pid);

            // Step 1: Send SIGTERM to the entire process group
            // This ensures all child processes created by the multiwatch command are also terminated
            kill(-process_pid, SIGTERM);

            // Step 2: Wait for graceful termination with timeout
            int termination_status;
            int wait_attempts = 0;
            const int max_wait_attempts = 10; // 10 * 100ms = 1 second total timeout
            
            while (waitpid(process_pid, &termination_status, WNOHANG) == 0 && 
                   wait_attempts < max_wait_attempts)
            {
                usleep(100000); // Wait 100ms between checks
                wait_attempts++;
            }

            // Step 3: Force kill with SIGKILL if process still running after timeout
            if (waitpid(process_pid, &termination_status, WNOHANG) == 0)
            {
                printf("Process %d still running after SIGTERM, forcing termination with SIGKILL\n", process_pid);
                kill(-process_pid, SIGKILL); // Kill entire process group
                waitpid(process_pid, &termination_status, 0); // Wait for confirmation
            }

            // Step 4: Close the file descriptor if it's still open
            if (multiwatch_processes[process_index].fd != -1)
            {
                close(multiwatch_processes[process_index].fd);
                multiwatch_processes[process_index].fd = -1; // Mark as closed
                printf("Closed file descriptor for process %d\n", process_pid);
            }

            // Step 5: Remove the temporary file used for process communication
            char *temp_filename = multiwatch_processes[process_index].temp_file;
            if (unlink(temp_filename) == 0)
            {
                printf("Successfully removed temporary file: %s\n", temp_filename);
            }
            else if (errno != ENOENT) // ENOENT means file already doesn't exist
            {
                printf("Warning: Failed to remove temporary file %s: %s\n",
                       temp_filename, strerror(errno));
            }
            else
            {
                printf("Temporary file already removed: %s\n", temp_filename);
            }

            // Mark this process slot as inactive for reuse
            multiwatch_processes[process_index].active = 0;
            printf("Completed cleanup for process %d\n", process_pid);
        }
    }

    // Reset the multiwatch system state
    multiwatch_count = 0;
    printf("MultiWatch cleanup completed. All processes and resources cleaned up.\n");
}

// Function to monitor multiWatch processes and display their output in real-time
void monitor_multiwatch_processes(Display *display, Window window, GC gc, Tab *tab)
{
    struct pollfd file_descriptors[MAX_MULTIWATCH_COMMANDS];
    char read_buffer[MULTIWATCH_BUFFER_SIZE];
    int active_process_count = multiwatch_count;
    const int max_read_attempts = 50; // 50 * 100ms = 5 second maximum monitoring time
    int current_attempt = 0;

    printf("Starting to monitor %d multiWatch processes\n", active_process_count);

    // Main monitoring loop - continues while processes are active or we're within timeout
    while ((active_process_count > 0 || current_attempt < max_read_attempts) && multiwatch_mode)
    {
        // Step 1: Set up file descriptors for polling
        int pollable_count = 0;
        for (int process_index = 0; process_index < multiwatch_count; process_index++)
        {
            if (multiwatch_processes[process_index].active && 
                multiwatch_processes[process_index].fd != -1)
            {
                file_descriptors[pollable_count].fd = multiwatch_processes[process_index].fd;
                file_descriptors[pollable_count].events = POLLIN;  // Monitor for read readiness
                file_descriptors[pollable_count].revents = 0;
                pollable_count++;
            }
        }

        // Step 2: Try immediate non-blocking read from all file descriptors
        int data_was_read = 0;
        for (int process_index = 0; process_index < multiwatch_count; process_index++)
        {
            if (multiwatch_processes[process_index].active && 
                multiwatch_processes[process_index].fd != -1)
            {
                ssize_t bytes_read = read(multiwatch_processes[process_index].fd, 
                                         read_buffer, sizeof(read_buffer) - 1);
                
                if (bytes_read > 0)
                {
                    data_was_read = 1;
                    read_buffer[bytes_read] = '\0';

                    // Format and display the output with proper formatting
                    add_separator_line(tab);

                    // Create timestamped header for this command's output
                    time_t current_time = time(NULL);
                    struct tm *time_info = localtime(&current_time);
                    char timestamp[64];
                    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", time_info);

                    char output_header[128];
                    snprintf(output_header, sizeof(output_header), "%sMultiWatch [%s]:", 
                             timestamp, multiwatch_processes[process_index].command);
                    add_text_to_buffer(tab, output_header);

                    // Split output into lines and display each one
                    char *current_line = read_buffer;
                    char *line_break;

                    do
                    {
                        line_break = strchr(current_line, '\n');
                        if (line_break)
                        {
                            *line_break = '\0'; // Temporarily terminate at newline
                        }

                        // Only display non-empty lines
                        if (strlen(current_line) > 0)
                        {
                            char formatted_line[BUFFER_COLS];
                            snprintf(formatted_line, sizeof(formatted_line), "  %s", current_line);
                            add_text_to_buffer(tab, formatted_line);
                        }

                        if (line_break)
                        {
                            current_line = line_break + 1; // Move to next line
                        }
                    } while (line_break);

                    add_separator_line(tab);
                    draw_text_buffer(display, window, gc); // Update display
                }
                else if (bytes_read == 0)
                {
                    // End of file reached - process has closed its output
                    printf("Process %d reached EOF on output\n", multiwatch_processes[process_index].pid);
                    close(multiwatch_processes[process_index].fd);
                    multiwatch_processes[process_index].fd = -1;
                }
                else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    // Read error (not just "would block")
                    printf("Read error for process %d: %s\n", 
                           multiwatch_processes[process_index].pid, strerror(errno));
                    close(multiwatch_processes[process_index].fd);
                    multiwatch_processes[process_index].fd = -1;
                }
            }
        }

        // Step 3: Use poll() for efficient waiting when no immediate data available
        if (pollable_count > 0 && !data_was_read)
        {
            int poll_result = poll(file_descriptors, pollable_count, 100); // 100ms timeout

            if (poll_result > 0)
            {
                // Process file descriptors that have data ready
                int fd_tracker = 0;
                for (int process_index = 0; process_index < multiwatch_count; process_index++)
                {
                    if (multiwatch_processes[process_index].active && 
                        multiwatch_processes[process_index].fd != -1)
                    {
                        if (file_descriptors[fd_tracker].revents & POLLIN)
                        {
                            ssize_t bytes_read = read(multiwatch_processes[process_index].fd, 
                                                     read_buffer, sizeof(read_buffer) - 1);
                            if (bytes_read > 0)
                            {
                                read_buffer[bytes_read] = '\0';

                                // Format output with timestamp and command name
                                time_t current_time = time(NULL);
                                struct tm *time_info = localtime(&current_time);
                                char timestamp[64];
                                strftime(timestamp, sizeof(timestamp), "%H:%M:%S", time_info);

                                // Split and display each line
                                char *current_line = read_buffer;
                                char *line_break;

                                do
                                {
                                    line_break = strchr(current_line, '\n');
                                    if (line_break)
                                    {
                                        *line_break = '\0';
                                    }

                                    if (strlen(current_line) > 0)
                                    {
                                        char formatted_line[BUFFER_COLS];
                                        snprintf(formatted_line, sizeof(formatted_line), 
                                                 "[%s] %s: %s", timestamp, 
                                                 multiwatch_processes[process_index].command, 
                                                 current_line);
                                        add_text_to_buffer(tab, formatted_line);
                                        printf("Poll Output: %s\n", formatted_line);
                                    }

                                    if (line_break)
                                    {
                                        current_line = line_break + 1;
                                    }
                                } while (line_break);

                                draw_text_buffer(display, window, gc);
                            }
                        }
                        fd_tracker++;
                    }
                }
            }
        }

        // Step 4: Check process status and update active count
        active_process_count = 0;
        for (int process_index = 0; process_index < multiwatch_count; process_index++)
        {
            if (multiwatch_processes[process_index].active)
            {
                int process_status;
                pid_t wait_result = waitpid(multiwatch_processes[process_index].pid, 
                                           &process_status, WNOHANG);
                
                if (wait_result == multiwatch_processes[process_index].pid)
                {
                    // Process has terminated
                    printf("Process %d finished with exit status %d\n", 
                           multiwatch_processes[process_index].pid, WEXITSTATUS(process_status));
                    multiwatch_processes[process_index].active = 0;
                    
                    // Clean up file descriptor
                    if (multiwatch_processes[process_index].fd != -1)
                    {
                        close(multiwatch_processes[process_index].fd);
                        multiwatch_processes[process_index].fd = -1;
                    }

                    // Read any remaining output from temporary file
                    char final_buffer[1024];
                    int temp_fd = open(multiwatch_processes[process_index].temp_file, O_RDONLY);
                    if (temp_fd != -1)
                    {
                        ssize_t remaining_bytes = read(temp_fd, final_buffer, sizeof(final_buffer) - 1);
                        if (remaining_bytes > 0)
                        {
                            final_buffer[remaining_bytes] = '\0';
                            time_t current_time = time(NULL);
                            struct tm *time_info = localtime(&current_time);
                            char timestamp[64];
                            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", time_info);

                            // Display any final output that wasn't captured via pipe
                            char *current_line = final_buffer;
                            char *line_break;
                            do
                            {
                                line_break = strchr(current_line, '\n');
                                if (line_break)
                                    *line_break = '\0';
                                if (strlen(current_line) > 0)
                                {
                                    char final_line[BUFFER_COLS];
                                    snprintf(final_line, sizeof(final_line), "[%s] %s: %s",
                                             timestamp, multiwatch_processes[process_index].command, current_line);
                                    add_text_to_buffer(tab, final_line);
                                    printf("Final output: %s\n", final_line);
                                }
                                if (line_break)
                                    current_line = line_break + 1;
                            } while (line_break);
                            draw_text_buffer(display, window, gc);
                        }
                        close(temp_fd);
                    }

                    // Notify user that command completed
                    char completion_message[128];
                    snprintf(completion_message, sizeof(completion_message),
                             "Command '%s' finished", multiwatch_processes[process_index].command);
                    add_text_to_buffer(tab, completion_message);
                    draw_text_buffer(display, window, gc);
                }
                else if (wait_result == 0)
                {
                    // Process is still running
                    active_process_count++;
                }
                else
                {
                    // Error checking process status
                    printf("Error checking process %d: %s\n", 
                           multiwatch_processes[process_index].pid, strerror(errno));
                    multiwatch_processes[process_index].active = 0;
                    if (multiwatch_processes[process_index].fd != -1)
                    {
                        close(multiwatch_processes[process_index].fd);
                        multiwatch_processes[process_index].fd = -1;
                    }
                }
            }
        }

        current_attempt++;

        // Step 5: Check for user interruption (Ctrl+C) via X11 events
        int event_processed = 0;
        while (XPending(display) > 0 && !event_processed)
        {
            XEvent x_event;
            XNextEvent(display, &x_event);

            if (x_event.type == KeyPress)
            {
                KeySym key_symbol;
                char key_buffer[256];
                XLookupString(&x_event.xkey, key_buffer, sizeof(key_buffer) - 1, &key_symbol, NULL);

                // Check for Ctrl+C interruption
                if ((x_event.xkey.state & ControlMask) && key_symbol == XK_c)
                {
                    printf("Ctrl+C detected - stopping multiWatch monitoring\n");
                    add_text_to_buffer(tab, "Ctrl+C received - stopping multiWatch");
                    draw_text_buffer(display, window, gc);
                    cleanup_multiwatch();
                    multiwatch_mode = 0;
                    return;
                }
            }
            event_processed = 1;
        }

        // Step 6: Check for SIGINT signal
        if (signal_received && which_signal == SIGINT)
        {
            printf("SIGINT received - stopping multiWatch monitoring\n");
            signal_received = 0;
            which_signal = 0;
            add_text_to_buffer(tab, "SIGINT received - stopping multiWatch");
            draw_text_buffer(display, window, gc);
            cleanup_multiwatch();
            multiwatch_mode = 0;
            return;
        }

        // Step 7: Adaptive delay based on current state
        if (active_process_count == 0 && current_attempt < max_read_attempts)
        {
            // No active processes but still within timeout - brief delay
            usleep(100000); // 100ms
        }
        else if (active_process_count == 0)
        {
            break; // No active processes and timeout reached
        }
        else
        {
            // Processes still active - shorter delay for more responsive monitoring
            usleep(50000); // 50ms
        }
    }

    // Final cleanup and status update
    printf("multiWatch monitoring completed after %d attempts\n", current_attempt);
    cleanup_multiwatch();
    multiwatch_mode = 0;
    add_text_to_buffer(tab, "multiWatch completed");
    draw_text_buffer(display, window, gc);
}

// Function to handle multiWatch command - executes multiple commands in parallel and monitors their output
void handle_multiwatch_command(Display *display, Window window, GC gc, Tab *tab, const char *command)
{
    // Step 1: Parse quoted commands from: multiWatch "cmd1" "cmd2" "cmd3"
    char parsed_commands[MAX_MULTIWATCH_COMMANDS][MAX_COMMAND_LENGTH];
    int command_count = 0;

    // Create a safe working copy of the command string
    char command_copy[MAX_COMMAND_LENGTH];
    if (snprintf(command_copy, sizeof(command_copy), "%s", command) >= (int)sizeof(command_copy))
    {
        add_text_to_buffer(tab, "Error: Command too long for processing");
        draw_text_buffer(display, window, gc);
        return;
    }

    // Skip past "multiWatch" to get to the command arguments
    char *parse_ptr = command_copy + 10; // Length of "multiWatch"

    // Parse quoted commands from the argument string
    while (*parse_ptr != '\0' && command_count < MAX_MULTIWATCH_COMMANDS)
    {
        // Skip any leading whitespace
        while (*parse_ptr == ' ')
            parse_ptr++;

        // Look for quoted commands
        if (*parse_ptr == '"')
        {
            parse_ptr++; // Move past the opening quote
            char *command_start = parse_ptr;

            // Find the closing quote
            while (*parse_ptr != '"' && *parse_ptr != '\0')
                parse_ptr++;

            if (*parse_ptr == '"')
            {
                int command_length = parse_ptr - command_start;
                if (command_length > 0 && command_length < MAX_COMMAND_LENGTH - 1)
                {
                    // Extract the command safely
                    if (snprintf(parsed_commands[command_count], MAX_COMMAND_LENGTH, 
                                "%.*s", command_length, command_start) >= MAX_COMMAND_LENGTH)
                    {
                        add_text_to_buffer(tab, "Error: Individual command too long");
                        draw_text_buffer(display, window, gc);
                        return;
                    }
                    command_count++;
                }
                parse_ptr++; // Move past the closing quote
            }
            else
            {
                add_text_to_buffer(tab, "Error: Unclosed quote in multiWatch command");
                draw_text_buffer(display, window, gc);
                return;
            }
        }
        else if (*parse_ptr != '\0')
        {
            // Invalid syntax - commands must be quoted
            add_text_to_buffer(tab, "Error: Invalid multiWatch syntax - use: multiWatch \"cmd1\" \"cmd2\"");
            draw_text_buffer(display, window, gc);
            return;
        }
    }

    // Step 2: Validate parsed commands
    if (command_count == 0)
    {
        add_text_to_buffer(tab, "Usage: multiWatch \"command1\" \"command2\" ...");
        draw_text_buffer(display, window, gc);
        return;
    }

    if (command_count > MAX_MULTIWATCH_COMMANDS)
    {
        char error_message[256];
        snprintf(error_message, sizeof(error_message), 
                "Error: Too many commands specified (maximum: %d)", MAX_MULTIWATCH_COMMANDS);
        add_text_to_buffer(tab, error_message);
        draw_text_buffer(display, window, gc);
        return;
    }

    // Step 3: Notify user and initialize multiwatch system
    add_text_to_buffer(tab, "Starting multiWatch mode. Press Ctrl+C to stop.");
    draw_text_buffer(display, window, gc);

    multiwatch_count = command_count;
    multiwatch_mode = 1; // Enable multiwatch monitoring mode

    int successful_process_starts = 0;

    // Step 4: Create temporary files and fork processes for each command
    for (int command_index = 0; command_index < command_count; command_index++)
    {
        // Create a unique temporary filename for this command's output
        if (snprintf(multiwatch_processes[command_index].temp_file,
                     sizeof(multiwatch_processes[command_index].temp_file),
                     ".temp.multiwatch.%d.%d.%d.txt",
                     getpid(), command_index, (int)time(NULL)) >= 
                    (int)sizeof(multiwatch_processes[command_index].temp_file))
        {
            printf("Warning: Temporary filename too long for command %d\n", command_index);
            multiwatch_processes[command_index].active = 0;
            continue;
        }

        // Create and immediately close the temporary file (child process will write to it)
        int temp_fd = open(multiwatch_processes[command_index].temp_file, 
                          O_WRONLY | O_CREAT | O_TRUNC, 0600); // Secure permissions: owner read/write only
        if (temp_fd == -1)
        {
            printf("Error: Failed to create temporary file %s: %s\n", 
                   multiwatch_processes[command_index].temp_file, strerror(errno));
            multiwatch_processes[command_index].active = 0;
            continue;
        }
        close(temp_fd); // Close now, child will reopen

        // Fork a new process for this command
        pid_t child_pid = fork();

        if (child_pid == 0)
        {
            // CHILD PROCESS: Execute the command
            // Reset signal handlers to default behavior
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            // Redirect stdout and stderr to the temporary file
            int output_fd = open(multiwatch_processes[command_index].temp_file, 
                                O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (output_fd == -1)
            {
                fprintf(stderr, "Failed to open output file: %s\n", strerror(errno));
                exit(1);
            }

            // Redirect standard output and error to the file
            if (dup2(output_fd, STDOUT_FILENO) == -1)
            {
                fprintf(stderr, "Failed to redirect stdout: %s\n", strerror(errno));
                close(output_fd);
                exit(1);
            }
            if (dup2(output_fd, STDERR_FILENO) == -1)
            {
                fprintf(stderr, "Failed to redirect stderr: %s\n", strerror(errno));
                close(output_fd);
                exit(1);
            }
            close(output_fd); // File descriptor no longer needed

            // Execute the command based on whether it contains pipes
            if (strstr(parsed_commands[command_index], "|") != NULL)
            {
                // Command contains pipes - execute through shell for proper pipe handling
                execl("/bin/sh", "sh", "-c", parsed_commands[command_index], NULL);
                fprintf(stderr, "Failed to execute command through shell: %s\n", strerror(errno));
            }
            else
            {
                // Simple command without pipes - tokenize and execute directly
                char *arguments[64];
                int argument_count = 0;
                char command_buffer[MAX_COMMAND_LENGTH];

                // Create a safe working copy for tokenization
                if (snprintf(command_buffer, sizeof(command_buffer), "%s", 
                            parsed_commands[command_index]) >= (int)sizeof(command_buffer))
                {
                    fprintf(stderr, "Error: Command too long for processing\n");
                    exit(1);
                }

                // Tokenize the command into arguments
                char *token = strtok(command_buffer, " ");
                while (token != NULL && argument_count < 63)
                {
                    arguments[argument_count++] = token;
                    token = strtok(NULL, " ");
                }
                arguments[argument_count] = NULL; // NULL-terminate the argument list

                if (argument_count == 0)
                {
                    fprintf(stderr, "Error: Empty command\n");
                    exit(1);
                }

                // Execute the command directly
                execvp(arguments[0], arguments);
                fprintf(stderr, "Failed to execute %s: %s\n", arguments[0], strerror(errno));
            }
            exit(127); // Exit with error if exec fails
        }
        else if (child_pid > 0)
        {
            // PARENT PROCESS: Track the child process
            multiwatch_processes[command_index].pid = child_pid;
            
            // Store the command string for display purposes
            if (snprintf(multiwatch_processes[command_index].command, MAX_COMMAND_LENGTH, 
                        "%s", parsed_commands[command_index]) >= MAX_COMMAND_LENGTH)
            {
                printf("Warning: Command name truncated for display\n");
            }
            multiwatch_processes[command_index].active = 1;

            // Open the temporary file for reading (non-blocking) to monitor output
            multiwatch_processes[command_index].fd = 
                open(multiwatch_processes[command_index].temp_file, O_RDONLY | O_NONBLOCK);
            if (multiwatch_processes[command_index].fd == -1)
            {
                printf("Failed to open temp file for reading: %s: %s\n", 
                       multiwatch_processes[command_index].temp_file, strerror(errno));
                multiwatch_processes[command_index].active = 0;
            }
            else
            {
                successful_process_starts++;
            }

            printf("Started process %d for command: %s\n", child_pid, parsed_commands[command_index]);
        }
        else
        {
            // Fork failed
            printf("Fork failed for command '%s': %s\n", 
                   parsed_commands[command_index], strerror(errno));
            multiwatch_processes[command_index].active = 0;
        }
    }

    // Step 5: Check if we successfully started any processes
    if (successful_process_starts == 0)
    {
        add_text_to_buffer(tab, "Error: Failed to start any multiWatch processes");
        cleanup_multiwatch();
        multiwatch_mode = 0;
        draw_text_buffer(display, window, gc);
        return;
    }

    // Step 6: Begin monitoring the running processes
    printf("Starting monitoring for %d multiWatch processes\n", successful_process_starts);
    monitor_multiwatch_processes(display, window, gc, tab);
}

void handle_jobs_command(Tab *tab)
{
    // Step 1: Check if there are any background jobs to display
    if (bg_job_count == 0)
    {
        add_text_to_buffer(tab, "No background jobs running");
        return;
    }

    // Step 2: Clean up finished processes before displaying
    // This ensures we only show currently active jobs
    for (int job_index = 0; job_index < bg_job_count; job_index++)
    {
        int process_status;
        pid_t wait_result = waitpid(bg_processes[job_index].pid, &process_status, WNOHANG);
        
        if (wait_result > 0)
        {
            // Process has terminated - remove it from the jobs array
            printf("Background job %d (PID: %d) has finished\n", 
                   bg_processes[job_index].job_id, bg_processes[job_index].pid);
            
            // Shift remaining jobs left to fill the gap
            for (int shift_index = job_index; shift_index < bg_job_count - 1; shift_index++)
            {
                bg_processes[shift_index] = bg_processes[shift_index + 1];
            }
            
            bg_job_count--;  // Reduce job count
            job_index--;     // Re-check current position since array was shifted
            
            printf("Removed finished job, now %d background jobs remaining\n", bg_job_count);
        }
        else if (wait_result == 0)
        {
            // Process is still running - no action needed
            continue;
        }
        else
        {
            // Error checking process status
            printf("Error checking status of job %d (PID: %d): %s\n",
                   bg_processes[job_index].job_id, bg_processes[job_index].pid, strerror(errno));
        }
    }

    // Step 3: Check if all jobs finished during cleanup
    if (bg_job_count == 0)
    {
        add_text_to_buffer(tab, "No background jobs running (all completed)");
        return;
    }

    // Step 4: Display information for all active background jobs
    add_text_to_buffer(tab, "Active background jobs:");
    
    for (int job_index = 0; job_index < bg_job_count; job_index++)
    {
        char job_display[256];
        
        // Format: "[1] Running    sleep 60"
        //         "[2] Stopped    vim file.txt"
        snprintf(job_display, sizeof(job_display), "[%d] %s    %s",
                 bg_processes[job_index].job_id, 
                 bg_processes[job_index].status, 
                 bg_processes[job_index].command);
        
        add_text_to_buffer(tab, job_display);
        
        // Optional: Add PID information for advanced users
        char pid_info[128];
        snprintf(pid_info, sizeof(pid_info), "     PID: %d", bg_processes[job_index].pid);
        add_text_to_buffer(tab, pid_info);
    }
    
    // Step 5: Add usage hint for job management
    add_text_to_buffer(tab, "Use 'fg <job_id>' to bring a job to foreground");
    add_text_to_buffer(tab, "Use 'kill %<job_id>' to terminate a background job");
}

void handle_fg_command(Tab *tab, const char *command)
{
    // Step 1: Check if there are any background jobs available
    if (bg_job_count == 0)
    {
        add_text_to_buffer(tab, "fg: no current job");
        return;
    }

    // Step 2: Parse the job ID from the command
    int target_job_id = -1;
    if (sscanf(command, "fg %d", &target_job_id) != 1)
    {
        // No job ID specified - use the most recently created job
        target_job_id = job_counter; // job_counter tracks the most recent job ID
        printf("No job ID specified, using most recent job: %d\n", target_job_id);
    }

    // Step 3: Find the specified job in the background processes array
    int found_job_index = -1;
    for (int job_index = 0; job_index < bg_job_count; job_index++)
    {
        if (bg_processes[job_index].job_id == target_job_id)
        {
            found_job_index = job_index;
            break;
        }
    }

    if (found_job_index == -1)
    {
        char error_message[256];
        snprintf(error_message, sizeof(error_message), "fg: job not found: %d", target_job_id);
        add_text_to_buffer(tab, error_message);
        
        // Show available jobs to help the user
        char available_jobs[256];
        snprintf(available_jobs, sizeof(available_jobs), 
                 "Available jobs: %d to %d", 
                 bg_processes[0].job_id, 
                 bg_processes[bg_job_count - 1].job_id);
        add_text_to_buffer(tab, available_jobs);
        return;
    }

    // Step 4: Prepare to bring the job to foreground
    pid_t target_pid = bg_processes[found_job_index].pid;
    char *target_command = bg_processes[found_job_index].command;

    // Step 5: Resume the job if it's stopped
    if (strcmp(bg_processes[found_job_index].status, "Stopped") == 0)
    {
        printf("Resuming stopped job %d (PID: %d)\n", target_job_id, target_pid);
        
        if (kill(target_pid, SIGCONT) == -1)
        {
            char error_message[256];
            snprintf(error_message, sizeof(error_message), 
                     "fg: failed to resume job %d: %s", target_job_id, strerror(errno));
            add_text_to_buffer(tab, error_message);
            return;
        }
        
        // Update job status to reflect it's now running
        strncpy(bg_processes[found_job_index].status, "Running", sizeof(bg_processes[found_job_index].status) - 1);
    }

    // Step 6: Set the process as foreground process in the tab
    tab->foreground_pid = target_pid;

    char status_message[256];
    snprintf(status_message, sizeof(status_message), 
             "Resumed job [%d] in foreground: %s", target_job_id, target_command);
    add_text_to_buffer(tab, status_message);
    printf("Brought job %d (PID: %d) to foreground: %s\n", target_job_id, target_pid, target_command);

    // Step 7: Wait for the process to complete
    int process_status;
    pid_t wait_result = waitpid(target_pid, &process_status, 0);

    if (wait_result == -1)
    {
        char error_message[256];
        snprintf(error_message, sizeof(error_message), 
                 "fg: error waiting for job %d: %s", target_job_id, strerror(errno));
        add_text_to_buffer(tab, error_message);
    }
    else
    {
        // Report how the process terminated
        if (WIFEXITED(process_status))
        {
            char exit_message[256];
            snprintf(exit_message, sizeof(exit_message), 
                     "Job [%d] exited with status %d", target_job_id, WEXITSTATUS(process_status));
            add_text_to_buffer(tab, exit_message);
            printf("Job %d exited normally with status %d\n", target_job_id, WEXITSTATUS(process_status));
        }
        else if (WIFSIGNALED(process_status))
        {
            char signal_message[256];
            snprintf(signal_message, sizeof(signal_message), 
                     "Job [%d] terminated by signal %d", target_job_id, WTERMSIG(process_status));
            add_text_to_buffer(tab, signal_message);
            printf("Job %d terminated by signal %d\n", target_job_id, WTERMSIG(process_status));
        }
        else if (WIFSTOPPED(process_status))
        {
            char stopped_message[256];
            snprintf(stopped_message, sizeof(stopped_message), 
                     "Job [%d] stopped by signal %d", target_job_id, WSTOPSIG(process_status));
            add_text_to_buffer(tab, stopped_message);
            printf("Job %d stopped by signal %d\n", target_job_id, WSTOPSIG(process_status));
        }
    }

    // Step 8: Remove the job from background processes array since it's now complete
    for (int shift_index = found_job_index; shift_index < bg_job_count - 1; shift_index++)
    {
        bg_processes[shift_index] = bg_processes[shift_index + 1];
    }
    bg_job_count--;
    
    // Clear the foreground PID since the process has terminated
    tab->foreground_pid = -1;
    
    printf("Removed job %d from background jobs list. %d jobs remaining.\n", 
           target_job_id, bg_job_count);
}

// Function to execute a command and capture its output
void execute_command(Display *display, Window window, GC gc, Tab *tab, const char *command)
{
    // Step 1: Parameter validation
    if (!display || !window || !gc || !tab)
    {
        printf("Error: Invalid parameters to execute_command\n");
        return;
    }

    if (command == NULL || strlen(command) == 0)
    {
        add_text_to_buffer(tab, "");
        return;
    }

    printf("Executing command: '%s'\n", command);

    // Step 2: Security validation
    if (!is_safe_command(command))
    {
        add_text_to_buffer(tab, "Error: Command contains potentially unsafe patterns");
        return;
    }

    // Step 3: Create safe working copy of command
    char command_copy[MAX_COMMAND_LENGTH];
    if (snprintf(command_copy, sizeof(command_copy), "%s", command) >= (int)sizeof(command_copy))
    {
        add_text_to_buffer(tab, "Error: Command too long");
        return;
    }
    command_copy[sizeof(command_copy) - 1] = '\0';

    // Step 4: Handle special built-in commands
    // Check for multiWatch command first
    if (strncmp(command, "multiWatch", 10) == 0)
    {
        handle_multiwatch_command(display, window, gc, tab, command);
        return;
    }

    // Tokenize command for built-in command checking
    char *args[64] = {0};
    int arg_count = 0;
    char *token = strtok(command_copy, " ");
    while (token != NULL && arg_count < 63)
    {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    // Handle built-in commands that don't need forking
    if (arg_count > 0 && strcmp(args[0], "cd") == 0)
    {
        char *path = ".";
        if (arg_count > 1)
        {
            path = args[1];
        }

        if (chdir(path) == -1)
        {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "cd: %s: %s", path, strerror(errno));
            add_text_to_buffer(tab, error_msg);
        }
        else
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
            {
                char success_msg[256];
                snprintf(success_msg, sizeof(success_msg), "Changed to directory: %s", cwd);
                add_text_to_buffer(tab, success_msg);
            }
            else
            {
                add_text_to_buffer(tab, "Changed directory (but cannot get current path)");
            }
        }
        return;
    }

    if (arg_count > 0 && strcmp(args[0], "history") == 0)
    {
        handle_history_command(tab);
        return;
    }

    if (arg_count > 0 && strcmp(args[0], "jobs") == 0)
    {
        handle_jobs_command(tab);
        return;
    }

    if (arg_count > 0 && strncmp(args[0], "fg", 2) == 0)
    {
        handle_fg_command(tab, command);
        return;
    }

    // Step 5: Prepare command execution with timestamp and visual formatting
    char command_header[512];
    time_t start_time = time(NULL);
    struct tm *tm_info = localtime(&start_time);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S]", tm_info);
    snprintf(command_header, sizeof(command_header), "%s Executing: %s", timestamp, command);
    add_text_to_buffer(tab, command_header);
    add_separator_line(tab);

    // Step 6: Parse command for pipes (single command vs pipeline)
    int num_commands = 1;
    char *commands[16];
    char command_copy2[MAX_COMMAND_LENGTH];

    if (snprintf(command_copy2, sizeof(command_copy2), "%s", command) >= (int)sizeof(command_copy2))
    {
        add_text_to_buffer(tab, "Error: Command too long");
        add_separator_line(tab); // Close the section even on error
        return;
    }
    command_copy2[sizeof(command_copy2) - 1] = '\0';

    commands[0] = strtok(command_copy2, "|");
    while ((commands[num_commands] = strtok(NULL, "|")) != NULL && num_commands < 15)
    {
        num_commands++;
    }

    // Step 7: Execute single command (no pipes)
    if (num_commands == 1)
    {
        int pipefd[2];
        pid_t pid;

        if (pipe(pipefd) == -1)
        {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Error: Failed to create pipe: %s", strerror(errno));
            add_text_to_buffer(tab, error_msg);
            add_separator_line(tab);
            return;
        }

        pid = fork();

        if (pid == -1)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Error: Fork failed: %s", strerror(errno));
            add_text_to_buffer(tab, error_msg);
            add_separator_line(tab);
            return;
        }
        else if (pid == 0)
        {
            // CHILD PROCESS: Set up redirection and execute command
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // Close unused read end of pipe
            if (close(pipefd[0]) == -1)
            {
                perror("close pipefd[0] failed");
                exit(1);
            }

            // Redirect stdout and stderr to pipe
            if (dup2(pipefd[1], STDOUT_FILENO) == -1)
            {
                perror("dup2 stdout failed");
                exit(1);
            }
            if (dup2(pipefd[1], STDERR_FILENO) == -1)
            {
                perror("dup2 stderr failed");
                exit(1);
            }
            if (close(pipefd[1]) == -1)
            {
                perror("close pipefd[1] failed");
            }

            // Parse command for I/O redirection and arguments
            char *args[64];
            int arg_count = 0;
            char *input_file = NULL;
            char *output_file = NULL;
            char cmd_copy[MAX_COMMAND_LENGTH];

            if (snprintf(cmd_copy, sizeof(cmd_copy), "%s", command) >= (int)sizeof(cmd_copy))
            {
                fprintf(stderr, "Error: Command too long\n");
                exit(1);
            }
            cmd_copy[sizeof(cmd_copy) - 1] = '\0';

            char *token = strtok(cmd_copy, " ");
            while (token != NULL && arg_count < 63)
            {
                if (strcmp(token, "<") == 0)
                {
                    token = strtok(NULL, " ");
                    if (token != NULL)
                        input_file = token;
                }
                else if (strcmp(token, ">") == 0)
                {
                    token = strtok(NULL, " ");
                    if (token != NULL)
                        output_file = token;
                }
                else
                {
                    args[arg_count++] = token;
                }
                token = strtok(NULL, " ");
            }
            args[arg_count] = NULL;

            // Handle input redirection
            if (input_file != NULL)
            {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1)
                {
                    fprintf(stderr, "Error: Cannot open input file '%s': %s\n", input_file, strerror(errno));
                    exit(1);
                }
                if (dup2(fd, STDIN_FILENO) == -1)
                {
                    fprintf(stderr, "Error: Cannot redirect stdin: %s\n", strerror(errno));
                    close(fd);
                    exit(1);
                }
                close(fd);
            }

            // Handle output redirection
            if (output_file != NULL)
            {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1)
                {
                    fprintf(stderr, "Error: Cannot create output file '%s': %s\n", output_file, strerror(errno));
                    exit(1);
                }
                if (dup2(fd, STDOUT_FILENO) == -1)
                {
                    fprintf(stderr, "Error: Cannot redirect stdout: %s\n", strerror(errno));
                    close(fd);
                    exit(1);
                }
                close(fd);
            }

            if (arg_count == 0)
            {
                fprintf(stderr, "Error: No command specified\n");
                exit(1);
            }

            // Execute the command
            execvp(args[0], args);
            fprintf(stderr, "Error: Command not found: %s (%s)\n", args[0], strerror(errno));
            exit(127);
        }
        else
        {
            // PARENT PROCESS: Monitor child and capture output
            tab->foreground_pid = pid;

            int status;
            char buffer[1024];
            ssize_t bytes_read;
            char full_output[OUTPUT_BUFFER_SIZE] = {0};
            int full_output_len = 0;

            // Close unused write end of pipe
            if (close(pipefd[1]) == -1)
            {
                printf("Warning: Failed to close pipe write end: %s\n", strerror(errno));
            }

            // Set non-blocking mode for responsive UI
            if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) == -1)
            {
                printf("Warning: Failed to set non-blocking mode: %s\n", strerror(errno));
            }

            int child_exited = 0;
            int timeout_counter = 0;
            const int MAX_TIMEOUT = 300; // 3 second timeout

            // Main monitoring loop
            while (!child_exited && timeout_counter < MAX_TIMEOUT)
            {
                // Check for user input (Ctrl+C, Ctrl+Z)
                if (XPending(display) > 0)
                {
                    XEvent ev;
                    XNextEvent(display, &ev);
                    if (ev.type == KeyPress)
                    {
                        KeySym keysym;
                        char keybuf[256];
                        XLookupString(&ev.xkey, keybuf, sizeof(keybuf) - 1, &keysym, NULL);

                        if ((ev.xkey.state & ControlMask) && keysym == XK_c)
                        {
                            printf("\nCtrl+C detected - interrupting process\n");
                            if (kill(pid, SIGINT) == -1)
                            {
                                printf("Warning: Failed to send SIGINT: %s\n", strerror(errno));
                            }
                            break;
                        }
                        else if ((ev.xkey.state & ControlMask) && keysym == XK_z)
                        {
                            printf("\nCtrl+Z detected - stopping process\n");
                            if (kill(pid, SIGTSTP) == -1)
                            {
                                printf("Warning: Failed to send SIGTSTP: %s\n", strerror(errno));
                            }
                            break;
                        }
                    }
                }

                // Check for signals received
                if (signal_received)
                {
                    signal_received = 0;
                    if (which_signal == SIGINT)
                    {
                        printf("\nCtrl+C received - interrupting process\n");
                        if (kill(pid, SIGINT) == -1)
                        {
                            printf("Warning: Failed to send SIGINT: %s\n", strerror(errno));
                        }
                        break;
                    }
                    else if (which_signal == SIGTSTP)
                    {
                        printf("\nCtrl+Z received - stopping process\n");
                        if (kill(pid, SIGTSTP) == -1)
                        {
                            printf("Warning: Failed to send SIGTSTP: %s\n", strerror(errno));
                        }
                        break;
                    }
                    which_signal = 0;
                }

                // Read available output from child
                bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0)
                {
                    buffer[bytes_read] = '\0';
                    if (full_output_len + bytes_read < OUTPUT_BUFFER_SIZE - 1)
                    {
                        strncpy(full_output + full_output_len, buffer, bytes_read);
                        full_output_len += bytes_read;
                        full_output[full_output_len] = '\0';
                    }
                    else
                    {
                        add_text_to_buffer(tab, "Warning: Output truncated (too large)");
                        break;
                    }
                }
                else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    printf("Read error: %s\n", strerror(errno));
                    break;
                }

                // Check if child has exited
                int wait_result = waitpid(pid, &status, WNOHANG);
                if (wait_result == pid)
                {
                    child_exited = 1;
                }
                else if (wait_result == -1)
                {
                    printf("Waitpid error: %s\n", strerror(errno));
                    break;
                }

                usleep(10000); // 10ms delay to prevent busy waiting
                timeout_counter++;
            }

            // Handle timeout
            if (timeout_counter >= MAX_TIMEOUT)
            {
                add_text_to_buffer(tab, "Error: Command timed out");
                if (kill(pid, SIGKILL) == -1)
                {
                    printf("Warning: Failed to kill timed out process: %s\n", strerror(errno));
                }
            }

            // Read any remaining data after child exit
            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
            {
                buffer[bytes_read] = '\0';
                if (full_output_len + bytes_read < OUTPUT_BUFFER_SIZE - 1)
                {
                    strncpy(full_output + full_output_len, buffer, bytes_read);
                    full_output_len += bytes_read;
                    full_output[full_output_len] = '\0';
                }
            }

            // Clean up pipe
            if (close(pipefd[0]) == -1)
            {
                printf("Warning: Failed to close pipe read end: %s\n", strerror(errno));
            }

            // Ensure no zombie processes
            if (!child_exited)
            {
                if (waitpid(pid, &status, WNOHANG) == 0)
                {
                    if (kill(pid, SIGKILL) == -1)
                    {
                        printf("Warning: Failed to kill process: %s\n", strerror(errno));
                    }
                    waitpid(pid, &status, 0);
                }
            }

            tab->foreground_pid = -1;

            // Step 8: Display results with appropriate formatting
            if (full_output_len > 0)
            {
                add_text_to_buffer(tab, full_output);
            }
            else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Command failed with exit code %d", WEXITSTATUS(status));
                add_text_to_buffer(tab, error_msg);
            }
            else if (WIFSIGNALED(status))
            {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Command terminated by signal %d", WTERMSIG(status));
                add_text_to_buffer(tab, error_msg);
            }
            else
            {
                add_text_to_buffer(tab, "(Command executed successfully - no output)");
            }

            // Add final separator to visually separate command output
            add_separator_line(tab);
        }
    }
    else
    {
        // Step 9: Handle piped commands (multiple commands connected with |)
        int pipefds[2][2]; // Alternating pipes for command chaining
        pid_t pids[16];    // Process IDs for all commands in pipeline
        int final_output_pipe[2]; // Final pipe to capture overall output

        if (pipe(final_output_pipe) == -1)
        {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Error: Failed to create output pipe: %s", strerror(errno));
            add_text_to_buffer(tab, error_msg);
            return;
        }

        // Create pipes for all commands except the last one
        for (int i = 0; i < num_commands - 1; i++)
        {
            if (pipe(pipefds[i % 2]) == -1)
            {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Error: Failed to create pipe: %s", strerror(errno));
                add_text_to_buffer(tab, error_msg);
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);
                return;
            }
        }

        // Fork all child processes for the pipeline
        for (int i = 0; i < num_commands; i++)
        {
            pids[i] = fork();

            if (pids[i] == -1)
            {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Error: Fork failed: %s", strerror(errno));
                add_text_to_buffer(tab, error_msg);

                // Clean up: kill already forked processes
                for (int j = 0; j < i; j++)
                {
                    kill(pids[j], SIGKILL);
                }
                // Close all pipes
                for (int j = 0; j < num_commands - 1; j++)
                {
                    close(pipefds[j % 2][0]);
                    close(pipefds[j % 2][1]);
                }
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);
                return;
            }
            else if (pids[i] == 0)
            {
                // CHILD PROCESS: Set up pipe connections and execute
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                // Set up input redirection from previous command
                if (i > 0)
                {
                    if (dup2(pipefds[(i - 1) % 2][0], STDIN_FILENO) == -1)
                    {
                        perror("dup2 stdin failed");
                        exit(1);
                    }
                }

                // Set up output redirection to next command or final output
                if (i < num_commands - 1)
                {
                    if (dup2(pipefds[i % 2][1], STDOUT_FILENO) == -1)
                    {
                        perror("dup2 stdout failed");
                        exit(1);
                    }
                    if (dup2(pipefds[i % 2][1], STDERR_FILENO) == -1)
                    {
                        perror("dup2 stderr failed");
                        exit(1);
                    }
                }
                else
                {
                    // Last command - redirect to final output pipe
                    if (dup2(final_output_pipe[1], STDOUT_FILENO) == -1)
                    {
                        perror("dup2 stdout failed");
                        exit(1);
                    }
                    if (dup2(final_output_pipe[1], STDERR_FILENO) == -1)
                    {
                        perror("dup2 stderr failed");
                        exit(1);
                    }
                }

                // Close all pipe file descriptors in child
                for (int j = 0; j < num_commands - 1; j++)
                {
                    close(pipefds[j % 2][0]);
                    close(pipefds[j % 2][1]);
                }
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);

                // Parse and execute the individual command
                char *args[64];
                int arg_count = 0;
                char cmd_copy[MAX_COMMAND_LENGTH];

                char *cmd = commands[i];
                // Trim leading and trailing whitespace
                char *end;
                while (*cmd == ' ')
                    cmd++;
                end = cmd + strlen(cmd) - 1;
                while (end > cmd && *end == ' ')
                {
                    *end = '\0';
                    end--;
                }

                if (snprintf(cmd_copy, sizeof(cmd_copy), "%s", cmd) >= (int)sizeof(cmd_copy))
                {
                    fprintf(stderr, "Error: Command too long\n");
                    exit(1);
                }
                cmd_copy[sizeof(cmd_copy) - 1] = '\0';

                char *token = strtok(cmd_copy, " ");
                while (token != NULL && arg_count < 63)
                {
                    args[arg_count++] = token;
                    token = strtok(NULL, " ");
                }
                args[arg_count] = NULL;

                if (arg_count == 0)
                {
                    fprintf(stderr, "Error: No command specified\n");
                    exit(1);
                }

                execvp(args[0], args);
                fprintf(stderr, "Error: Command not found: %s (%s)\n", args[0], strerror(errno));
                exit(127);
            }
        }

        // PARENT PROCESS: Close unused pipe ends and monitor pipeline
        for (int i = 0; i < num_commands - 1; i++)
        {
            if (close(pipefds[i % 2][0]) == -1)
            {
                printf("Warning: Failed to close pipe read end: %s\n", strerror(errno));
            }
            if (close(pipefds[i % 2][1]) == -1)
            {
                printf("Warning: Failed to close pipe write end: %s\n", strerror(errno));
            }
        }
        if (close(final_output_pipe[1]) == -1)
        {
            printf("Warning: Failed to close final output pipe write end: %s\n", strerror(errno));
        }

        tab->foreground_pid = pids[num_commands - 1];

        // Capture output from the final pipe
        int status;
        char buffer[1024];
        ssize_t bytes_read;
        char full_output[OUTPUT_BUFFER_SIZE] = {0};
        int full_output_len = 0;

        if (fcntl(final_output_pipe[0], F_SETFL, O_NONBLOCK) == -1)
        {
            printf("Warning: Failed to set non-blocking mode: %s\n", strerror(errno));
        }

        int all_children_exited = 0;
        int timeout_counter = 0;
        const int MAX_TIMEOUT = 500; // 5 seconds max for pipeline

        // Monitor pipeline execution
        while (!all_children_exited && timeout_counter < MAX_TIMEOUT)
        {
            bytes_read = read(final_output_pipe[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                if (full_output_len + bytes_read < OUTPUT_BUFFER_SIZE - 1)
                {
                    strncpy(full_output + full_output_len, buffer, bytes_read);
                    full_output_len += bytes_read;
                    full_output[full_output_len] = '\0';
                }
                else
                {
                    add_text_to_buffer(tab, "Warning: Output truncated (too large)");
                    break;
                }
            }
            else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                printf("Read error: %s\n", strerror(errno));
                break;
            }

            // Check if all children have exited
            all_children_exited = 1;
            for (int i = 0; i < num_commands; i++)
            {
                if (waitpid(pids[i], &status, WNOHANG) == 0)
                {
                    all_children_exited = 0;
                }
                else if (waitpid(pids[i], &status, WNOHANG) == -1)
                {
                    printf("Waitpid error for process %d: %s\n", pids[i], strerror(errno));
                }
            }

            usleep(10000);
            timeout_counter++;
        }

        // Handle pipeline timeout
        if (timeout_counter >= MAX_TIMEOUT)
        {
            add_text_to_buffer(tab, "Error: Pipeline timed out");
            for (int i = 0; i < num_commands; i++)
            {
                if (kill(pids[i], SIGKILL) == -1)
                {
                    printf("Warning: Failed to kill process %d: %s\n", pids[i], strerror(errno));
                }
            }
        }

        // Read any remaining data
        while ((bytes_read = read(final_output_pipe[0], buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[bytes_read] = '\0';
            if (full_output_len + bytes_read < OUTPUT_BUFFER_SIZE - 1)
            {
                strncpy(full_output + full_output_len, buffer, bytes_read);
                full_output_len += bytes_read;
                full_output[full_output_len] = '\0';
            }
        }

        if (close(final_output_pipe[0]) == -1)
        {
            printf("Warning: Failed to close final output pipe read end: %s\n", strerror(errno));
        }

        // Wait for all children to prevent zombies
        for (int i = 0; i < num_commands; i++)
        {
            if (waitpid(pids[i], NULL, 0) == -1)
            {
                printf("Warning: Failed to wait for process %d: %s\n", pids[i], strerror(errno));
            }
        }

        tab->foreground_pid = -1;

        // Display pipeline output
        if (full_output_len > 0)
        {
            add_text_to_buffer(tab, full_output);
        }
        else
        {
            add_text_to_buffer(tab, "");
        }
        add_separator_line(tab);
    }

    // Step 10: Update the display with the new content
    draw_text_buffer(display, window, gc);
}

// Function to handle Enter key - executes command and shows output
void handle_enter_key(Display *display, Window window, GC gc, Tab *tab)
{
    // Step 1: Check if there's a command to execute
    if (wcslen(tab->current_command) > 0)
    {
        // Convert wide character command back to multibyte for execution
        char multibyte_command[MAX_COMMAND_LENGTH * 4];
        size_t converted_chars = wcstombs(multibyte_command, tab->current_command, sizeof(multibyte_command) - 1);
        
        if (converted_chars == (size_t)-1)
        {
            // UTF-8 conversion failed, use simple ASCII fallback
            for (int char_index = 0; 
                 char_index < tab->command_length && char_index < sizeof(multibyte_command) - 1; 
                 char_index++)
            {
                multibyte_command[char_index] = (char)tab->current_command[char_index];
            }
            multibyte_command[tab->command_length] = '\0';
        }
        else
        {
            multibyte_command[converted_chars] = '\0';
        }

        // Add command to history before execution
        add_to_history(tab, multibyte_command);
        printf("ENTER pressed in tab '%s' - executing command: '%s'\n", tab->tab_name, multibyte_command);
    }
    else
    {
        printf("ENTER pressed with empty command in tab '%s'\n", tab->tab_name);
    }

    // Step 2: Clear the current cursor position for clean display
    tab->text_buffer[tab->cursor_row][tab->cursor_col] = L' ';

    // Step 3: Move to next line for command output display
    if (tab->cursor_row >= BUFFER_ROWS - 1)
    {
        // At bottom of buffer - scroll to make space
        scroll_buffer(tab);
    }
    else
    {
        // Move to next line within available space
        tab->cursor_row++;
    }
    tab->cursor_col = 0; // Reset to beginning of line

    // Step 4: Execute the command if one was entered
    if (wcslen(tab->current_command) > 0)
    {
        // Convert to multibyte for execution (repeating conversion for clarity)
        char multibyte_command[MAX_COMMAND_LENGTH * 4];
        size_t converted_chars = wcstombs(multibyte_command, tab->current_command, sizeof(multibyte_command) - 1);
        
        if (converted_chars == (size_t)-1)
        {
            // Fallback to ASCII conversion
            for (int char_index = 0; 
                 char_index < tab->command_length && char_index < sizeof(multibyte_command) - 1; 
                 char_index++)
            {
                multibyte_command[char_index] = (char)tab->current_command[char_index];
            }
            multibyte_command[tab->command_length] = '\0';
        }
        else
        {
            multibyte_command[converted_chars] = '\0';
        }

        // Execute the command - this will handle output display and separators
        execute_command(display, window, gc, tab, multibyte_command);
    }
    else
    {
        // Empty command - just add a blank line
        add_text_to_buffer(tab, "");
    }

    // Step 5: Prepare for next command input
    if (tab->cursor_row >= BUFFER_ROWS - 1)
    {
        // If we're at the bottom after command execution, scroll again
        scroll_buffer(tab);
    }
    else
    {
        // Move to next line for the new command prompt
        tab->cursor_row++;
        tab->cursor_col = 0;
    }

    // Step 6: Set up the command prompt for new input
    tab->text_buffer[tab->cursor_row][0] = L'>';  // Command prompt character
    tab->text_buffer[tab->cursor_row][1] = L' ';  // Space after prompt
    tab->cursor_col = 2; // Position cursor after "> " prompt

    // Step 7: Reset command buffer for new input
    memset(tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
    tab->command_length = 0;
    tab->cursor_buffer_pos = 0;

    // Step 8: Update the display to show the new state
    draw_text_buffer(display, window, gc);
    
    printf("Command execution completed in tab '%s'. Ready for new input.\n", tab->tab_name);
}

// Function to handle keyboard input for the terminal
void handle_keypress(Display *display, Window window, GC gc, XKeyEvent *key_event)
{
    // Step 1: Get the currently active tab
    Tab *active_tab = &tabs[active_tab_index];

    // Step 2: Process the key event and convert to usable format
    char key_buffer[UTF8_BUFFER_SIZE];
    KeySym key_symbol;
    int buffer_length;
    int shift_pressed = (key_event->state & ShiftMask);
    int control_pressed = (key_event->state & ControlMask);
    int alt_pressed = (key_event->state & Mod1Mask);

    // Convert X11 key event to string representation
    buffer_length = XLookupString(key_event, key_buffer, sizeof(key_buffer) - 1, &key_symbol, NULL);
    key_buffer[buffer_length] = '\0';

    // Convert input to wide character for consistent internal handling
    wchar_t wide_character = L'\0';
    if (buffer_length > 0)
    {
        int conversion_result = mbtowc(&wide_character, key_buffer, buffer_length);
        if (conversion_result == -1)
        {
            wide_character = L'\0'; // Conversion failed
        }
    }

    // Step 3: Handle different keys based on the key symbol
    switch (key_symbol)
    {
    case XK_Escape:
        if (active_tab->search_mode)
        {
            // ESC in search mode: exit search and clear state
            active_tab->search_mode = 0;
            memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
            active_tab->command_length = 0;
            active_tab->cursor_buffer_pos = 0;
            active_tab->search_pos = 0;
            memset(active_tab->search_buffer, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
            update_command_display(active_tab);
        }
        else
        {
            // ESC in normal mode: exit application
            printf("ESC pressed - exiting application\n");
            exit(0);
        }
        break;

    case XK_Return:
    case XK_KP_Enter:
        if (active_tab->search_mode)
        {
            // ENTER in search mode: execute search and exit search mode
            active_tab->search_mode = 0;

            if (active_tab->search_pos > 0)
            {
                wchar_t found_command[MAX_COMMAND_LENGTH] = {0};
                int search_result = search_history(active_tab, active_tab->search_buffer, found_command, 1);

                if (search_result == 1)
                {
                    // Single match found - use it as current command
                    wcscpy(active_tab->current_command, found_command);
                    active_tab->command_length = wcslen(active_tab->current_command);
                    active_tab->cursor_buffer_pos = active_tab->command_length;
                }
                else if (search_result > 1)
                {
                    // Multiple matches found - show them to user
                    add_text_to_buffer(active_tab, "");
                    add_text_to_buffer(active_tab, "Multiple matches found:");

                    char message[256];
                    snprintf(message, sizeof(message), "Found %d matches. Refine your search.", search_result);
                    add_text_to_buffer(active_tab, message);

                    // Clear command since we have multiple matches
                    memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
                    active_tab->command_length = 0;
                    active_tab->cursor_buffer_pos = 0;
                }
                else if (search_result == -1)
                {
                    // No matches found
                    add_text_to_buffer(active_tab, "No match for search term in history");
                    memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
                    active_tab->command_length = 0;
                    active_tab->cursor_buffer_pos = 0;
                }
                else
                {
                    // No match found (result == 0)
                    memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
                    active_tab->command_length = 0;
                    active_tab->cursor_buffer_pos = 0;
                }
            }
            else
            {
                // Empty search - clear command
                memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
                active_tab->command_length = 0;
                active_tab->cursor_buffer_pos = 0;
            }

            // Reset search state
            active_tab->search_pos = 0;
            memset(active_tab->search_buffer, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));

            // Return to normal command display
            update_command_display(active_tab);
        }
        else
        {
            // ENTER in normal mode: execute the current command
            handle_enter_key(display, window, gc, active_tab);
        }
        break;

    case XK_n:
        if (control_pressed)
        {
            // Ctrl+N: Create new tab
            printf("Ctrl+N pressed - creating new tab\n");
            create_new_tab();
            
            // Switch to the newly created tab
            if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
            {
                tabs[active_tab_index].active = 0;
            }
            active_tab_index = tab_count - 1;
            if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
            {
                tabs[active_tab_index].active = 1;
                update_command_display(&tabs[active_tab_index]);
            }
            draw_text_buffer(display, window, gc);
            break;
        }
        goto default_case;

    case XK_Tab:
        if (control_pressed)
        {
            // Ctrl+Tab: Switch to next tab
            if (tab_count > 0 && tab_count <= MAX_TABS)
            {
                // Deactivate current tab
                if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
                {
                    tabs[active_tab_index].active = 0;
                }
                // Move to next tab (wrap around)
                active_tab_index = (active_tab_index + 1) % tab_count;
                // Activate new tab
                if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
                {
                    tabs[active_tab_index].active = 1;
                    update_command_display(&tabs[active_tab_index]);
                }
            }
        }
        else if (!active_tab->search_mode)
        {
            // Tab key: Trigger command completion
            handle_tab_completion(active_tab);
        }
        break;

    case XK_w:
        if (control_pressed)
        {
            // Ctrl+W: Close current tab
            close_current_tab();
            draw_text_buffer(display, window, gc);
            break;
        }
        goto default_case;

    case XK_BackSpace:
    case XK_Delete:
        if (active_tab->search_mode)
        {
            // Backspace in search mode: remove last search character
            if (active_tab->search_pos > 0)
            {
                active_tab->search_pos--;
                active_tab->search_buffer[active_tab->search_pos] = L'\0';

                // Update search display with current results
                char search_prompt[256];
                char multibyte_search[MAX_COMMAND_LENGTH * 4] = {0};
                size_t converted_chars = wcstombs(multibyte_search, active_tab->search_buffer, sizeof(multibyte_search) - 1);
                
                if (converted_chars == (size_t)-1)
                {
                    // Fallback conversion
                    for (int char_index = 0; char_index < active_tab->search_pos && char_index < sizeof(multibyte_search) - 1; char_index++)
                    {
                        multibyte_search[char_index] = (char)active_tab->search_buffer[char_index];
                    }
                    multibyte_search[active_tab->search_pos] = '\0';
                }

                // Perform search with updated term
                wchar_t found_command[MAX_COMMAND_LENGTH] = {0};
                int search_result = search_history(active_tab, active_tab->search_buffer, found_command, 0);

                if (search_result == 1)
                {
                    char multibyte_found[MAX_COMMAND_LENGTH * 4] = {0};
                    wcstombs(multibyte_found, found_command, sizeof(multibyte_found) - 1);
                    multibyte_found[sizeof(multibyte_found) - 1] = '\0';
                    snprintf(search_prompt, sizeof(search_prompt), "(reverse-i-search)`%s': %s", multibyte_search, multibyte_found);
                }
                else
                {
                    snprintf(search_prompt, sizeof(search_prompt), "(reverse-i-search)`%s': ", multibyte_search);
                }

                update_command_display_with_prompt(active_tab, search_prompt);
            }
            else
            {
                // No more characters to delete - show empty search prompt
                update_command_display_with_prompt(active_tab, "(reverse-i-search)`': ");
            }
        }
        else
        {
            // Backspace in normal mode: delete character before cursor
            if (active_tab->cursor_buffer_pos > 0)
            {
                // Shift characters left to overwrite deleted character
                for (int char_index = active_tab->cursor_buffer_pos - 1; char_index < active_tab->command_length; char_index++)
                {
                    active_tab->current_command[char_index] = active_tab->current_command[char_index + 1];
                }
                active_tab->command_length--;
                active_tab->cursor_buffer_pos--;
                update_command_display(active_tab);
            }
        }
        break;

    case XK_Left:
        // Move cursor left
        if (!active_tab->search_mode && active_tab->cursor_buffer_pos > 0)
        {
            active_tab->cursor_buffer_pos--;
            update_command_display(active_tab);
        }
        break;

    case XK_Right:
        // Move cursor right
        if (!active_tab->search_mode && active_tab->cursor_buffer_pos < active_tab->command_length)
        {
            active_tab->cursor_buffer_pos++;
            update_command_display(active_tab);
        }
        break;

    case XK_Up:
        // Navigate command history backwards
        if (!active_tab->search_mode && active_tab->history_current > 0)
        {
            active_tab->history_current--;
            wcscpy(active_tab->current_command, active_tab->command_history[active_tab->history_current]);
            active_tab->command_length = wcslen(active_tab->current_command);
            active_tab->cursor_buffer_pos = active_tab->command_length;
            update_command_display(active_tab);
        }
        break;

    case XK_Down:
        // Navigate command history forwards
        if (!active_tab->search_mode)
        {
            if (active_tab->history_current < active_tab->history_count - 1)
            {
                active_tab->history_current++;
                wcscpy(active_tab->current_command, active_tab->command_history[active_tab->history_current]);
                active_tab->command_length = wcslen(active_tab->current_command);
                active_tab->cursor_buffer_pos = active_tab->command_length;
            }
            else if (active_tab->history_current == active_tab->history_count - 1)
            {
                // Reached the end - clear command for new input
                active_tab->history_current = active_tab->history_count;
                memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH * sizeof(wchar_t));
                active_tab->command_length = 0;
                active_tab->cursor_buffer_pos = 0;
            }
            update_command_display(active_tab);
        }
        break;

    case XK_a:
        if (control_pressed && !active_tab->search_mode)
        {
            // Ctrl+A: Move cursor to beginning of line
            active_tab->cursor_buffer_pos = 0;
            update_command_display(active_tab);
            break;
        }
        goto default_case;

    case XK_e:
        if (control_pressed && !active_tab->search_mode)
        {
            // Ctrl+E: Move cursor to end of line
            active_tab->cursor_buffer_pos = active_tab->command_length;
            update_command_display(active_tab);
            break;
        }
        goto default_case;

    case XK_r:
        if (control_pressed && !active_tab->search_mode)
        {
            // Ctrl+R: Enter reverse search mode
            enter_search_mode(active_tab);
            break;
        }
        goto default_case;

    case XK_space:
        // Insert space character
        if (!active_tab->search_mode && active_tab->command_length < MAX_COMMAND_LENGTH - 1)
        {
            // Make space for new character
            for (int char_index = active_tab->command_length; char_index > active_tab->cursor_buffer_pos; char_index--)
            {
                active_tab->current_command[char_index] = active_tab->current_command[char_index - 1];
            }
            active_tab->current_command[active_tab->cursor_buffer_pos] = L' ';
            active_tab->command_length++;
            active_tab->current_command[active_tab->command_length] = L'\0';
            active_tab->cursor_buffer_pos++;
            update_command_display(active_tab);
        }
        break;

    case XK_Page_Up:
        // Scroll buffer up
        if (!active_tab->search_mode)
        {
            scroll_up(active_tab);
            draw_text_buffer(display, window, gc);
        }
        break;

    case XK_Page_Down:
        // Scroll buffer down
        if (!active_tab->search_mode)
        {
            scroll_down(active_tab);
            draw_text_buffer(display, window, gc);
        }
        break;

    case XK_End:
        if (control_pressed && !active_tab->search_mode)
        {
            // Ctrl+End: Move cursor to end of line (same as Ctrl+E)
            active_tab->cursor_buffer_pos = active_tab->command_length;
            update_command_display(active_tab);
            break;
        }
        // End key: Scroll to bottom of buffer
        if (active_tab->scrollback_offset > 0 && !control_pressed)
        {
            scroll_to_bottom(active_tab);
            draw_text_buffer(display, window, gc);
            break;
        }
        goto default_case;

    case XK_Home:
        if (control_pressed && !active_tab->search_mode)
        {
            // Ctrl+Home: Move cursor to beginning of line (same as Ctrl+A)
            active_tab->cursor_buffer_pos = 0;
            update_command_display(active_tab);
            break;
        }
        // Home key: Scroll to top of buffer
        if (!control_pressed && active_tab->scrollback_count > BUFFER_ROWS - 1)
        {
            active_tab->scrollback_offset = active_tab->max_scrollback_offset;
            render_scrollback(active_tab);
            draw_text_buffer(display, window, gc);
            break;
        }
        goto default_case;

    default_case:
    default:
        // Handle regular character input
        if (active_tab->search_mode)
        {
            // Character input in search mode: add to search buffer
            if (wide_character != L'\0' && iswprint(wide_character))
            {
                if (active_tab->search_pos < MAX_COMMAND_LENGTH - 1)
                {
                    active_tab->search_buffer[active_tab->search_pos] = wide_character;
                    active_tab->search_pos++;
                    active_tab->search_buffer[active_tab->search_pos] = L'\0';

                    // Update search display with current results
                    char search_prompt[256];
                    char multibyte_search[MAX_COMMAND_LENGTH * 4] = {0};

                    // Convert search term for display
                    size_t converted_chars = wcstombs(multibyte_search, active_tab->search_buffer, sizeof(multibyte_search) - 1);
                    if (converted_chars == (size_t)-1)
                    {
                        // Fallback conversion
                        for (int char_index = 0; char_index < active_tab->search_pos && char_index < sizeof(multibyte_search) - 1; char_index++)
                        {
                            multibyte_search[char_index] = (char)active_tab->search_buffer[char_index];
                        }
                        multibyte_search[active_tab->search_pos] = '\0';
                    }

                    wchar_t found_command[MAX_COMMAND_LENGTH] = {0};
                    int search_result = search_history(active_tab, active_tab->search_buffer, found_command, 0);

                    if (search_result == 1)
                    {
                        char multibyte_found[MAX_COMMAND_LENGTH * 4] = {0};
                        wcstombs(multibyte_found, found_command, sizeof(multibyte_found) - 1);
                        multibyte_found[sizeof(multibyte_found) - 1] = '\0';
                        snprintf(search_prompt, sizeof(search_prompt), "(reverse-i-search)`%s': %s", multibyte_search, multibyte_found);
                    }
                    else
                    {
                        snprintf(search_prompt, sizeof(search_prompt), "(reverse-i-search)`%s': ", multibyte_search);
                    }

                    update_command_display_with_prompt(active_tab, search_prompt);
                }
            }
        }
        else
        {
            // Character input in normal mode: add to command buffer
            if (wide_character != L'\0' && iswprint(wide_character) && !control_pressed)
            {
                if (active_tab->command_length < MAX_COMMAND_LENGTH - 1)
                {
                    // Make space for new character at cursor position
                    for (int char_index = active_tab->command_length; char_index > active_tab->cursor_buffer_pos; char_index--)
                    {
                        active_tab->current_command[char_index] = active_tab->current_command[char_index - 1];
                    }
                    active_tab->current_command[active_tab->cursor_buffer_pos] = wide_character;
                    active_tab->command_length++;
                    active_tab->current_command[active_tab->command_length] = L'\0';
                    active_tab->cursor_buffer_pos++;
                    update_command_display(active_tab);
                }
            }
        }
        break;
    }

    // Step 4: Update display for all keys except Enter (which handles its own display)
    if (key_symbol != XK_Return && key_symbol != XK_KP_Enter)
    {
        draw_text_buffer(display, window, gc);
    }
}

// Updated signal handlers for graceful process management

/**
 * SIGINT handler - handles Ctrl+C interrupt signal
 * This signal typically requests graceful termination of the foreground process
 */
void handle_sigint(int sig)
{
    // Set global flags to indicate signal was received
    signal_received = 1;
    which_signal = SIGINT;

    // Note: Multiwatch mode handling is commented out but available if needed
    // if (multiwatch_mode) {
    //     cleanup_multiwatch();
    //     multiwatch_mode = 0;
    // }
    
    printf("SIGINT (Ctrl+C) received - signal handling initiated\n");
}

/**
 * SIGTSTP handler - handles Ctrl+Z stop signal
 * This signal suspends the foreground process and moves it to background jobs
 * Similar to how bash handles job control with Ctrl+Z
 */
void handle_sigtstp(int sig)
{
    // Set global flags to indicate signal was received
    signal_received = 1;
    which_signal = SIGTSTP;

    // Get reference to the currently active tab
    Tab *active_tab = &tabs[active_tab_index];

    // Check if there's an active foreground process to suspend
    if (active_tab->foreground_pid > 0)
    {
        printf("SIGTSTP (Ctrl+Z) received - suspending foreground process %d\n", active_tab->foreground_pid);
        
        // Send SIGSTOP to suspend the foreground process (not terminate)
        if (kill(active_tab->foreground_pid, SIGSTOP) == -1)
        {
            printf("Warning: Failed to send SIGSTOP to process %d: %s\n",
                   active_tab->foreground_pid, strerror(errno));
            return;
        }

        // Add the stopped process to background jobs array for later management
        if (bg_job_count < MAX_BG_JOBS)
        {
            // Store process information in background jobs array
            bg_processes[bg_job_count].pid = active_tab->foreground_pid;
            strcpy(bg_processes[bg_job_count].status, "Stopped");

            // Convert the current command from wide characters to multibyte for storage
            char multibyte_command[MAX_COMMAND_LENGTH * 4];
            size_t converted_chars = wcstombs(multibyte_command, active_tab->current_command, sizeof(multibyte_command) - 1);
            
            if (converted_chars == (size_t)-1)
            {
                // Conversion failed - use fallback description
                strcpy(bg_processes[bg_job_count].command, "unknown");
            }
            else
            {
                // Safe copy of command string with null termination
                strncpy(bg_processes[bg_job_count].command, multibyte_command, MAX_COMMAND_LENGTH - 1);
                bg_processes[bg_job_count].command[MAX_COMMAND_LENGTH - 1] = '\0';
            }

            // Assign unique job ID and increment job counter
            bg_processes[bg_job_count].job_id = ++job_counter;
            bg_job_count++;

            // Notify user that process was stopped and added to background jobs
            char job_message[256];
            snprintf(job_message, sizeof(job_message), 
                     "[%d] Stopped    %s", job_counter, bg_processes[bg_job_count - 1].command);
            add_text_to_buffer(active_tab, job_message);
            
            printf("Process %d stopped and added to background jobs as job [%d]\n", 
                   active_tab->foreground_pid, job_counter);
        }
        else
        {
            printf("Warning: Cannot add process to background - maximum jobs (%d) reached\n", MAX_BG_JOBS);
        }

        // Clear the foreground PID since process is now in background
        active_tab->foreground_pid = -1;
    }
    else
    {
        printf("SIGTSTP received but no foreground process to suspend\n");
    }
}

/**
 * X11 Error Handler - Custom error handler for X11 library errors
 * 
 * This function intercepts X11 errors that would normally cause the program
 * to terminate abruptly. Instead, it provides meaningful error messages
 * while allowing the application to continue running when possible.
 * 
 * @param display Pointer to the X11 display connection
 * @param error_event Pointer to the XErrorEvent structure containing error details
 * @return Always returns 0 to indicate the error was handled
 */
static int x11_error_handler(Display *display, XErrorEvent *error_event)
{
    char error_description[256];
    
    // Convert X11 error code to human-readable description
    XGetErrorText(display, error_event->error_code, error_description, sizeof(error_description));
    
    // Log the error with detailed information for debugging
    printf("X11 Error Detected:\n");
    printf("  Description: %s\n", error_description);
    printf("  Request Code: %d (indicates which X11 operation failed)\n", error_event->request_code);
    printf("  Error Code: %d (specific X11 error type)\n", error_event->error_code);
    printf("  Resource ID: %lu (the X11 resource that caused the error)\n", error_event->resourceid);
    printf("  Minor Code: %d (additional operation-specific information)\n", error_event->minor_code);
    
    // Common X11 error codes and their meanings for easier debugging
    switch (error_event->error_code) {
        case BadWindow:
            printf("  Note: BadWindow error - invalid window ID specified\n");
            break;
        case BadMatch:
            printf("  Note: BadMatch error - parameter mismatch in X11 request\n");
            break;
        case BadAccess:
            printf("  Note: BadAccess error - attempt to access protected resource\n");
            break;
        case BadAlloc:
            printf("  Note: BadAlloc error - insufficient memory or resources\n");
            break;
        case BadValue:
            printf("  Note: BadValue error - numeric parameter out of range\n");
            break;
        case BadAtom:
            printf("  Note: BadAtom error - invalid atom parameter\n");
            break;
        default:
            printf("  Note: See X11 protocol documentation for error code details\n");
            break;
    }
    
    // Return 0 to indicate we've handled the error and prevent X11 from terminating the program
    return 0;
}

int main()
{
    // Step 1: Initialize localization for Unicode and internationalization support
    if (setlocale(LC_ALL, "") == NULL)
    {
        fprintf(stderr, "Warning: Failed to set locale - Unicode support may be limited\n");
    }

    // Step 2: Declare X11 variables
    Display *display;
    Window window;
    XEvent event;
    int screen;
    GC graphics_context;
    
    // Note: atexit cleanup is commented out but available if needed
    // atexit(cleanup_resources_default);

    // Step 3: Install signal handlers for graceful process management
    printf("Installing signal handlers...\n");
    if (signal(SIGINT, handle_sigint) == SIG_ERR)
    {
        fprintf(stderr, "Warning: Failed to set SIGINT (Ctrl+C) handler\n");
    }
    if (signal(SIGTSTP, handle_sigtstp) == SIG_ERR)
    {
        fprintf(stderr, "Warning: Failed to set SIGTSTP (Ctrl+Z) handler\n");
    }
    if (signal(SIGSEGV, handle_sigsegv) == SIG_ERR)
    {
        fprintf(stderr, "Warning: Failed to set SIGSEGV (segmentation fault) handler\n");
    }

    // Step 4: Initialize the text buffer system and create first tab
    printf("Initializing text buffer system...\n");
    initialize_text_buffer();

    // Step 5: Initialize background jobs tracking system
    printf("Initializing background jobs system...\n");
    memset(bg_processes, 0, sizeof(bg_processes));
    job_counter = 0;
    bg_job_count = 0;

    // Step 6: Connect to X11 display server
    printf("Connecting to X11 display server...\n");
    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "Error: Cannot open X11 display\n");
        fprintf(stderr, "  Check that X11 server is running and DISPLAY environment variable is set\n");
        fprintf(stderr, "  Try: echo $DISPLAY (should show something like :0)\n");
        exit(1);
    }

    screen = DefaultScreen(display);

    // Step 7: Calculate window dimensions based on character grid
    int window_width = BUFFER_COLS * CHAR_WIDTH;
    int window_height = BUFFER_ROWS * CHAR_HEIGHT;

    // Step 8: Create the main application window
    printf("Creating main window (%dx%d pixels)...\n", window_width, window_height);
    window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        100, 100,                    // Initial x, y position
        window_width, window_height,  // Width and height
        2,                           // Border width
        BlackPixel(display, screen), // Border color
        WhitePixel(display, screen)  // Background color
    );

    if (window == 0)
    {
        fprintf(stderr, "Error: Failed to create X11 window\n");
        XCloseDisplay(display);
        exit(1);
    }

    // Step 9: Create graphics context for drawing operations
    printf("Creating graphics context...\n");
    graphics_context = XCreateGC(display, window, 0, NULL);
    if (graphics_context == NULL)
    {
        fprintf(stderr, "Error: Failed to create graphics context\n");
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        exit(1);
    }

    XSetForeground(display, graphics_context, BlackPixel(display, screen));

    // Step 10: Select which events the window will receive
    XSelectInput(display, window,
                 ExposureMask |        // Window expose/redraw events
                 KeyPressMask |        // Keyboard key press events
                 KeyReleaseMask |      // Keyboard key release events  
                 ButtonPressMask |     // Mouse button events
                 StructureNotifyMask   // Window resize/move events
    );

    // Step 11: Make the window visible
    XMapWindow(display, window);

    // Step 12: Set window title
    if (XStoreName(display, window, "X11 Shell with Tabs") == 0)
    {
        printf("Warning: Failed to set window title\n");
    }

    // Step 13: Install custom X11 error handler for better debugging
    XSetErrorHandler(x11_error_handler);

    // Step 14: Enable proper window close protocol (WM_DELETE_WINDOW)
    Atom window_delete_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
    if (window_delete_atom != None)
    {
        XSetWMProtocols(display, window, &window_delete_atom, 1);
        printf("Window close protocol enabled\n");
    }
    else
    {
        printf("Warning: Failed to set window close protocol - may not close gracefully\n");
    }

    // Step 15: Display startup information and usage tips
    printf("\n=== X11 Shell Terminal Started Successfully ===\n");
    printf("Window dimensions: %d columns x %d rows of text\n", BUFFER_COLS, BUFFER_ROWS);
    printf("Character size: %dx%d pixels\n", CHAR_WIDTH, CHAR_HEIGHT);
    printf("Active tab: %s\n", tabs[active_tab_index].tab_name);
    printf("\nKeyboard Shortcuts:\n");
    printf("  Ctrl+N         - Create new tab\n");
    printf("  Ctrl+W         - Close current tab\n"); 
    printf("  Ctrl+Tab       - Switch to next tab\n");
    printf("  Ctrl+R         - Search command history\n");
    printf("  Ctrl+C         - Interrupt current process\n");
    printf("  Ctrl+Z         - Stop/suspend current process\n");
    printf("  Ctrl+A         - Move cursor to start of line\n");
    printf("  Ctrl+E         - Move cursor to end of line\n");
    printf("  Page Up/Down   - Scroll through output history\n");
    printf("  Click tabs     - Switch tabs with mouse\n");
    printf("  Mouse wheel    - Scroll through output\n");
    printf("  ESC            - Exit application\n");
    printf("\nReady for commands...\n\n");

    // Step 16: Main event processing loop
    while (1)
    {
        // Check if there are pending X11 events to process
        if (XPending(display) > 0)
        {
            XNextEvent(display, &event);

            switch (event.type)
            {
            case Expose:
                // Window needs redrawing (exposed, resized, etc.)
                printf("Debug: Expose event - redrawing window contents\n");
                draw_text_buffer(display, window, graphics_context);
                break;

            case KeyPress:
                // Keyboard key pressed - handle text input and commands
                handle_keypress(display, window, graphics_context, &event.xkey);
                break;

            case ButtonPress:
                // Mouse button pressed
                if (event.xbutton.y < CHAR_HEIGHT)
                {
                    // Click in tab header area - handle tab switching
                    handle_tab_click(event.xbutton.x);
                    draw_text_buffer(display, window, graphics_context);
                }
                else
                {
                    // Click in main content area or mouse wheel events
                    if (event.xbutton.button == 4)
                    {
                        // Mouse wheel up - scroll buffer up
                        scroll_up(&tabs[active_tab_index]);
                        draw_text_buffer(display, window, graphics_context);
                    }
                    else if (event.xbutton.button == 5)
                    {
                        // Mouse wheel down - scroll buffer down
                        scroll_down(&tabs[active_tab_index]);
                        draw_text_buffer(display, window, graphics_context);
                    }
                    else
                    {
                        // Regular mouse click - focus on window
                        printf("Debug: Mouse click - focusing window\n");
                        XSetInputFocus(display, window, RevertToParent, CurrentTime);
                    }
                }
                break;

            case ConfigureNotify:
                // Window configuration changed (resized, moved)
                // Note: Currently we don't handle resizing, but the event is captured
                break;

            case ClientMessage:
                // Handle window manager messages (like close request)
                {
                    Atom window_delete_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
                    if (window_delete_atom != None &&
                        event.xclient.data.l[0] == window_delete_atom)
                    {
                        printf("Window close request received - initiating graceful shutdown\n");
                        goto cleanup_and_exit;
                    }
                }
                break;
            }
        }

        // Step 17: Check for and process pending signals
        if (signal_received)
        {
            signal_received = 0;

            if (which_signal == SIGINT)
            {
                printf("\nSIGINT (Ctrl+C) detected in main loop\n");
                Tab *active_tab = &tabs[active_tab_index];
                if (active_tab->foreground_pid > 0)
                {
                    // Send interrupt signal to foreground process
                    if (kill(active_tab->foreground_pid, SIGINT) == -1)
                    {
                        printf("Warning: Failed to send SIGINT to process %d: %s\n",
                               active_tab->foreground_pid, strerror(errno));
                    }
                    else
                    {
                        printf("Successfully sent SIGINT to foreground process %d\n", active_tab->foreground_pid);
                    }
                    active_tab->foreground_pid = -1;
                }
                
                // Clean up multiwatch mode if active
                if (multiwatch_mode)
                {
                    printf("Cleaning up multiwatch processes due to SIGINT\n");
                    cleanup_multiwatch();
                    multiwatch_mode = 0;
                }
            }
            else if (which_signal == SIGTSTP)
            {
                printf("\nSIGTSTP (Ctrl+Z) detected in main loop\n");
                Tab *active_tab = &tabs[active_tab_index];
                if (active_tab->foreground_pid > 0)
                {
                    // Send stop signal to foreground process
                    if (kill(active_tab->foreground_pid, SIGSTOP) == -1)
                    {
                        printf("Warning: Failed to send SIGSTOP to process %d: %s\n",
                               active_tab->foreground_pid, strerror(errno));
                    }
                    else
                    {
                        printf("Successfully stopped foreground process %d\n", active_tab->foreground_pid);
                    }
                    active_tab->foreground_pid = -1;
                }
            }

            which_signal = 0;
        }

        // Brief sleep to prevent excessive CPU usage in event loop
        usleep(10000); // 10 milliseconds
    }

// Step 18: Cleanup and exit label
cleanup_and_exit:
    printf("Initiating application shutdown...\n");
    
    // Perform comprehensive resource cleanup
    cleanup_resources(display, window, graphics_context);
    
    printf("X11 Shell Terminal exited successfully.\n");
    return 0;
}
