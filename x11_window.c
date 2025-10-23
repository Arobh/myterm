#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/poll.h>
#include <time.h>
#include <errno.h>

#define MAX_MULTIWATCH_COMMANDS 10
#define MULTIWATCH_BUFFER_SIZE 1024

#define BUFFER_ROWS 25
#define BUFFER_COLS 80
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16
#define MAX_COMMAND_LENGTH 256
#define OUTPUT_BUFFER_SIZE 4096

#define MAX_TABS 10
#define MAX_TAB_NAME 32
#define MAX_HISTORY_SIZE 10000


typedef struct {
    pid_t pid;
    int fd;
    char command[MAX_COMMAND_LENGTH];
    char temp_file[64];
    int active;
} MultiWatchProcess;

MultiWatchProcess multiwatch_processes[MAX_MULTIWATCH_COMMANDS];
int multiwatch_count = 0;
int multiwatch_mode = 0;

typedef struct
{
    char text_buffer[BUFFER_ROWS][BUFFER_COLS];
    char current_command[MAX_COMMAND_LENGTH];
    int command_length;
    int cursor_row;
    int cursor_col;
    int cursor_buffer_pos;
    pid_t foreground_pid;
    char command_history[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH];
    int history_count;
    int history_current;
    int search_mode;
    char search_buffer[MAX_COMMAND_LENGTH];
    int search_pos;
    char tab_name[MAX_TAB_NAME];
    int active;
} Tab;

Tab tabs[MAX_TABS];
int tab_count = 1;
int active_tab_index = 0;

// // Global text buffer
// char text_buffer[BUFFER_ROWS][BUFFER_COLS];
// char current_command[MAX_COMMAND_LENGTH];
// int command_length = 0;
// int cursor_row = 1;
// int cursor_col = 0;
// int cursor_buffer_pos = 0;
// pid_t foreground_pid = -1;
volatile sig_atomic_t signal_received = 0;
volatile sig_atomic_t which_signal = 0;

// char command_history[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH];
// int history_count = 0;
// int history_current = -1;
// int search_mode = 0;
// char search_buffer[MAX_COMMAND_LENGTH] = {0};
// int search_pos = 0;

// Function prototypes
void update_command_display_with_prompt(Tab *tab, const char *prompt);
void handle_tab_completion(Tab *tab);
void enter_search_mode(Tab *tab);
int search_history(Tab *tab, const char *search_term, char *result);
void close_current_tab();
void add_text_to_buffer(Tab *tab, const char *text);
void handle_multiwatch_command(Display *display, Window window, GC gc, Tab *tab, const char *command);
void monitor_multiwatch_processes(Display *display, Window window, GC gc, Tab *tab);
void cleanup_multiwatch();

// Add this function
void close_current_tab()
{
    if (tab_count <= 1)
    {
        printf("Cannot close the last tab\n");
        return; // Don't close the last tab
    }
    // Kill any running process in the tab being closed
    if (tabs[active_tab_index].foreground_pid > 0)
    {
        kill(tabs[active_tab_index].foreground_pid, SIGTERM);
        waitpid(tabs[active_tab_index].foreground_pid, NULL, 0);
    }

    // Move all subsequent tabs up
    for (int i = active_tab_index; i < tab_count - 1; i++)
    {
        tabs[i] = tabs[i + 1];
    }

    tab_count--;
    if (active_tab_index >= tab_count)
    {
        active_tab_index = tab_count - 1;
    }
    tabs[active_tab_index].active = 1;
}

// Function to create a new tab
void create_new_tab()
{
    if (tab_count < MAX_TABS)
    {
        char tab_name[32];
        snprintf(tab_name, sizeof(tab_name), "Tab %d", tab_count + 1);

        Tab *new_tab = &tabs[tab_count];

        // Initialize the new tab properly
        memset(new_tab, 0, sizeof(Tab)); // Clear the entire structure

        new_tab->cursor_buffer_pos = 0;
        new_tab->command_length = 0;
        new_tab->cursor_row = BUFFER_ROWS - 1;
        new_tab->cursor_col = 2;
        new_tab->foreground_pid = -1;
        new_tab->history_count = 0;
        new_tab->history_current = -1;
        new_tab->search_mode = 0;
        new_tab->search_pos = 0;
        memset(new_tab->search_buffer, 0, MAX_COMMAND_LENGTH);
        strncpy(new_tab->tab_name, tab_name, MAX_TAB_NAME - 1);
        new_tab->tab_name[MAX_TAB_NAME - 1] = '\0';
        new_tab->active = 0;

        // Initialize text buffer
        for (int row = 0; row < BUFFER_ROWS; row++)
        {
            for (int col = 0; col < BUFFER_COLS; col++)
            {
                new_tab->text_buffer[row][col] = ' ';
            }
        }

        char *welcome_message = "Welcome to X11 Shell Terminal!";
        char *instructions = "Type commands like 'ls' or 'pwd' and press ENTER";

        int welcome_len = strlen(welcome_message);
        int welcome_start = (BUFFER_COLS - welcome_len) / 2;
        for (int i = 0; i < welcome_len && welcome_start + i < BUFFER_COLS; i++)
        {
            new_tab->text_buffer[1][welcome_start + i] = welcome_message[i];
        }

        int instr_len = strlen(instructions);
        int instr_start = (BUFFER_COLS - instr_len) / 2;
        for (int i = 0; i < instr_len && instr_start + i < BUFFER_COLS; i++)
        {
            new_tab->text_buffer[3][instr_start + i] = instructions[i];
        }

        new_tab->text_buffer[BUFFER_ROWS - 1][0] = '>';
        new_tab->text_buffer[BUFFER_ROWS - 1][1] = ' ';

        memset(new_tab->current_command, 0, MAX_COMMAND_LENGTH);

        tab_count++;
    }
    else
    {
        printf("Maximum tab limit (%d) reached\n", MAX_TABS);
    }
}

// Function to handle mouse clicks for tab switching
void handle_tab_click(int click_x)
{
    if (tab_count <= 0 || tab_count > MAX_TABS)
        return;

    // Ensure we don't divide by zero
    if (tab_count == 0)
        return;

    int tab_width = BUFFER_COLS / tab_count;
    // Ensure tab_width is at least 1 to avoid division by zero
    if (tab_width < 1)
        tab_width = 1;

    int clicked_tab = click_x / CHAR_WIDTH / tab_width;

    // Bounds checking
    if (clicked_tab >= 0 && clicked_tab < tab_count)
    {
        // Deactivate current tab
        if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
        {
            tabs[active_tab_index].active = 0;
        }
        // Activate new tab
        active_tab_index = clicked_tab;
        if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
        {
            tabs[active_tab_index].active = 1;
        }
    }
}

// Function to scroll the entire buffer up by one line
void scroll_buffer(Tab *tab)
{
    if (!tab)
        return;

    for (int row = 0; row < BUFFER_ROWS - 1; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            tab->text_buffer[row][col] = tab->text_buffer[row + 1][col];
        }
    }

    for (int col = 0; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[BUFFER_ROWS - 1][col] = ' ';
    }

    tab->cursor_row = BUFFER_ROWS - 1;

    // Ensure cursor column is within bounds
    if (tab->cursor_col >= BUFFER_COLS)
    {
        tab->cursor_col = BUFFER_COLS - 1;
    }
}

// Function to add text to the buffer, handling line breaks and scrolling
void add_text_to_buffer(Tab *tab, const char *text)
{
    if (!tab || !text)
        return;

    char output_buffer[OUTPUT_BUFFER_SIZE];
    strncpy(output_buffer, text, OUTPUT_BUFFER_SIZE - 1);
    output_buffer[OUTPUT_BUFFER_SIZE - 1] = '\0';

    char *line = output_buffer;
    char *newline;

    do
    {
        newline = strchr(line, '\n');
        if (newline)
        {
            *newline = '\0';
        }

        if (tab->cursor_row >= BUFFER_ROWS - 1)
        {
            scroll_buffer(tab);
        }
        else
        {
            tab->cursor_row++;
        }
        tab->cursor_col = 0;

        int line_len = strlen(line);
        int max_len = BUFFER_COLS - 1;
        int copy_len = line_len < max_len ? line_len : max_len;

        // Ensure we don't write outside buffer bounds
        if (copy_len > 0 && tab->cursor_row >= 0 && tab->cursor_row < BUFFER_ROWS)
        {
            for (int i = 0; i < copy_len && i < BUFFER_COLS; i++)
            {
                tab->text_buffer[tab->cursor_row][i] = line[i];
            }
        }

        if (newline)
        {
            line = newline + 1;
        }
    } while (newline);
}

// Function to update the command display with proper cursor positioning
void update_command_display(Tab *tab)
{
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = ' ';
    }

    tab->text_buffer[tab->cursor_row][0] = '>';
    tab->text_buffer[tab->cursor_row][1] = ' ';

    int display_col = 2;
    for (int i = 0; i < tab->command_length && display_col < BUFFER_COLS; i++)
    {
        tab->text_buffer[tab->cursor_row][display_col] = tab->current_command[i];
        display_col++;
    }

    tab->cursor_col = 2 + tab->cursor_buffer_pos;
}

// Function to handle Tab key auto-completion
void handle_tab_completion(Tab *tab)
{
    // Get the current word being typed (from last space to cursor)
    int word_start = tab->cursor_buffer_pos - 1;
    while (word_start >= 0 && tab->current_command[word_start] != ' ')
    {
        word_start--;
    }
    word_start++; // Move to first character of word

    int word_length = tab->cursor_buffer_pos - word_start;

    if (word_length <= 0)
    {
        return; // No word to complete
    }

    char current_word[MAX_COMMAND_LENGTH];
    strncpy(current_word, &tab->current_command[word_start], word_length);
    current_word[word_length] = '\0';

    // Scan current directory for matches
    DIR *dir = opendir(".");
    if (dir == NULL)
    {
        return;
    }

    struct dirent *entry;
    char matches[256][MAX_COMMAND_LENGTH];
    int match_count = 0;
    char common_prefix[MAX_COMMAND_LENGTH] = "";

    while ((entry = readdir(dir)) != NULL && match_count < 256)
    {
        // Skip hidden files unless the word starts with '.'
        if (entry->d_name[0] == '.' && current_word[0] != '.')
        {
            continue;
        }

        // Check if this entry matches our current word
        if (strncmp(entry->d_name, current_word, word_length) == 0)
        {
            strcpy(matches[match_count], entry->d_name);
            match_count++;

            // Build common prefix
            if (match_count == 1)
            {
                strcpy(common_prefix, entry->d_name);
            }
            else
            {
                // Find common prefix between current common_prefix and new match
                int i;
                for (i = 0; common_prefix[i] != '\0' && matches[match_count - 1][i] != '\0'; i++)
                {
                    if (common_prefix[i] != matches[match_count - 1][i])
                    {
                        break;
                    }
                }
                common_prefix[i] = '\0';
            }
        }
    }

    closedir(dir);

    if (match_count == 0)
    {
        // No matches found
        return;
    }
    else if (match_count == 1)
    {
        // Single match - complete it
        char *completion = matches[0];
        int completion_len = strlen(completion);

        // Replace current word with completion
        for (int i = 0; i < completion_len; i++)
        {
            if (word_start + i < MAX_COMMAND_LENGTH - 1)
            {
                tab->current_command[word_start + i] = completion[i];
            }
        }

        // Null terminate and update lengths
        int new_length = word_start + completion_len;
        if (new_length < MAX_COMMAND_LENGTH)
        {
            tab->current_command[new_length] = '\0';
            tab->command_length = new_length;
            tab->cursor_buffer_pos = new_length;
        }

        // Add space after completion if it's not the end of line
        if (tab->cursor_buffer_pos < MAX_COMMAND_LENGTH - 1 && tab->current_command[tab->cursor_buffer_pos] == '\0')
        {
            tab->current_command[tab->cursor_buffer_pos] = ' ';
            tab->command_length++;
            tab->current_command[tab->command_length] = '\0';
            tab->cursor_buffer_pos++;
        }
    }
    else
    {
        // Multiple matches
        if (strlen(common_prefix) > word_length)
        {
            // Complete to common prefix
            char *completion = common_prefix;
            int completion_len = strlen(completion);

            // Replace current word with common prefix
            for (int i = 0; i < completion_len; i++)
            {
                if (word_start + i < MAX_COMMAND_LENGTH - 1)
                {
                    tab->current_command[word_start + i] = completion[i];
                }
            }

            // Null terminate and update lengths
            int new_length = word_start + completion_len;
            if (new_length < MAX_COMMAND_LENGTH)
            {
                tab->current_command[new_length] = '\0';
                tab->command_length = new_length;
                tab->cursor_buffer_pos = new_length;
            }
        }

        // Show available matches
        printf("\n");                // New line in console
        add_text_to_buffer(tab, ""); // Add blank line in X11 display

        // Display matches in columns (simple formatting)
        char match_line[BUFFER_COLS] = {0};
        int line_pos = 0;

        for (int i = 0; i < match_count; i++)
        {
            int match_len = strlen(matches[i]);

            // Check if this match fits on current line
            if (line_pos + match_len + 2 > BUFFER_COLS)
            {
                add_text_to_buffer(tab, match_line);
                match_line[0] = '\0';
                line_pos = 0;
            }

            // Add match to line
            if (line_pos > 0)
            {
                strcat(match_line, "  ");
                line_pos += 2;
            }
            strcat(match_line, matches[i]);
            line_pos += match_len;
        }

        // Add remaining matches
        if (line_pos > 0)
        {
            add_text_to_buffer(tab, match_line);
        }

        // Show prompt again
        add_text_to_buffer(tab, "");
    }

    update_command_display(tab);
}

// Function to add a command to history
void add_to_history(Tab *tab, const char *command)
{
    if (strlen(command) == 0 ||
        (tab->history_count > 0 && strcmp(tab->command_history[tab->history_count - 1], command) == 0))
    {
        return;
    }

    if (tab->history_count < MAX_HISTORY_SIZE)
    {
        strncpy(tab->command_history[tab->history_count], command, MAX_COMMAND_LENGTH - 1);
        tab->command_history[tab->history_count][MAX_COMMAND_LENGTH - 1] = '\0';
        tab->history_count++;
    }
    else
    {
        for (int i = 1; i < MAX_HISTORY_SIZE; i++)
        {
            strcpy(tab->command_history[i - 1], tab->command_history[i]);
        }
        strncpy(tab->command_history[MAX_HISTORY_SIZE - 1], command, MAX_COMMAND_LENGTH - 1);
        tab->command_history[MAX_HISTORY_SIZE - 1][MAX_COMMAND_LENGTH - 1] = '\0';
    }
    tab->history_current = tab->history_count;
}

// Function to handle history built-in command
void handle_history_command(Tab *tab)
{
    int start = (tab->history_count > 10) ? tab->history_count - 10 : 0;
    for (int i = start; i < tab->history_count; i++)
    {
        char history_line[256];
        snprintf(history_line, sizeof(history_line), "%d: %s", i + 1, tab->command_history[i]);
        add_text_to_buffer(tab, history_line);
    }
}

// Function to search history
int search_history(Tab *tab, const char *search_term, char *result)
{
    for (int i = tab->history_count - 1; i >= 0; i--)
    {
        if (strstr(tab->command_history[i], search_term) != NULL)
        {
            strcpy(result, tab->command_history[i]);
            return 1;
        }
    }
    return 0;
}

// Function to enter search mode
void enter_search_mode(Tab *tab)
{
    tab->search_mode = 1;
    tab->search_pos = 0;
    tab->search_buffer[0] = '\0';

    memset(tab->current_command, 0, MAX_COMMAND_LENGTH);
    tab->command_length = 0;
    tab->cursor_buffer_pos = 0;

    update_command_display_with_prompt(tab, "(reverse-i-search)`': ");
}

// Function to update display with custom prompt
void update_command_display_with_prompt(Tab *tab, const char *prompt)
{
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        tab->text_buffer[tab->cursor_row][col] = ' ';
    }

    int prompt_len = strlen(prompt);
    for (int i = 0; i < prompt_len && i < BUFFER_COLS; i++)
    {
        tab->text_buffer[tab->cursor_row][i] = prompt[i];
    }

    int display_col = prompt_len;
    for (int i = 0; i < tab->search_pos && display_col < BUFFER_COLS; i++)
    {
        tab->text_buffer[tab->cursor_row][display_col] = tab->search_buffer[i];
        display_col++;
    }

    tab->cursor_col = prompt_len + tab->search_pos;
}

// Function to initialize a tab's text buffer
void initialize_tab(Tab *tab, const char *name)
{
    tab->cursor_buffer_pos = 0;
    tab->command_length = 0;
    tab->cursor_row = BUFFER_ROWS - 1;
    tab->cursor_col = 2;
    tab->foreground_pid = -1;
    tab->history_count = 0;
    tab->history_current = -1;
    tab->search_mode = 0;
    tab->search_pos = 0;
    memset(tab->search_buffer, 0, MAX_COMMAND_LENGTH);
    strncpy(tab->tab_name, name, MAX_TAB_NAME - 1);
    tab->tab_name[MAX_TAB_NAME - 1] = '\0';

    for (int row = 0; row < BUFFER_ROWS; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            tab->text_buffer[row][col] = ' ';
        }
    }

    char *welcome_message = "Welcome to X11 Shell Terminal!";
    char *instructions = "Type commands like 'ls' or 'pwd' and press ENTER";

    int welcome_len = strlen(welcome_message);
    int welcome_start = (BUFFER_COLS - welcome_len) / 2;
    for (int i = 0; i < welcome_len && welcome_start + i < BUFFER_COLS; i++)
    {
        tab->text_buffer[1][welcome_start + i] = welcome_message[i];
    }

    int instr_len = strlen(instructions);
    int instr_start = (BUFFER_COLS - instr_len) / 2;
    for (int i = 0; i < instr_len && instr_start + i < BUFFER_COLS; i++)
    {
        tab->text_buffer[3][instr_start + i] = instructions[i];
    }

    tab->text_buffer[BUFFER_ROWS - 1][0] = '>';
    tab->text_buffer[BUFFER_ROWS - 1][1] = ' ';

    memset(tab->current_command, 0, MAX_COMMAND_LENGTH);
}

// Updated initialize_text_buffer
void initialize_text_buffer()
{
    // Initialize all tabs to zero first
    memset(tabs, 0, sizeof(tabs));

    // Initialize first tab
    initialize_tab(&tabs[0], "Tab 1");
    tabs[0].active = 1;
    tab_count = 1;
    active_tab_index = 0;

    // Ensure initial state is valid
    if (active_tab_index >= tab_count)
    {
        active_tab_index = 0;
    }
}

// Function to draw the entire text buffer to the window
void draw_text_buffer(Display *display, Window window, GC gc)
{
    XClearWindow(display, window);

    // Safety checks
    if (tab_count <= 0 || tab_count > MAX_TABS)
        return;
    if (active_tab_index < 0 || active_tab_index >= tab_count)
        return;

    // Draw tab headers
    int tab_width = BUFFER_COLS / tab_count;
    if (tab_width < 1)
        tab_width = 1;

    for (int i = 0; i < tab_count && i < MAX_TABS; i++)
    {
        int x_start = i * tab_width;

        // Ensure we don't draw outside window bounds
        if (x_start < 0 || x_start >= BUFFER_COLS)
            continue;

        // In draw_text_buffer, improve tab appearance:
        if (i == active_tab_index)
        {
            XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));
            XFillRectangle(display, window, gc, x_start * CHAR_WIDTH, 0,
                           tab_width * CHAR_WIDTH, CHAR_HEIGHT);
            XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
        }
        else
        {
            XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
            XFillRectangle(display, window, gc, x_start * CHAR_WIDTH, 0,
                           tab_width * CHAR_WIDTH, CHAR_HEIGHT);
            XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));
        }

        // Ensure tab name fits in available space
        char tab_display[MAX_TAB_NAME];
        int max_chars = (tab_width - 2) > 0 ? (tab_width - 2) : 1;
        if (max_chars > MAX_TAB_NAME - 1)
            max_chars = MAX_TAB_NAME - 1;

        strncpy(tab_display, tabs[i].tab_name, max_chars);
        tab_display[max_chars] = '\0';

        XDrawString(display, window, gc,
                    (x_start + 1) * CHAR_WIDTH, CHAR_HEIGHT - 2,
                    tab_display, strlen(tab_display));
    }

    // Draw text content for active tab
    XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));

    Tab *active_tab = &tabs[active_tab_index];

    for (int row = 0; row < BUFFER_ROWS; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            if (active_tab->text_buffer[row][col] != ' ')
            {
                int x = col * CHAR_WIDTH;
                int y = (row + 1) * CHAR_HEIGHT;

                // Ensure we don't draw outside window
                if (x >= 0 && x < BUFFER_COLS * CHAR_WIDTH &&
                    y >= CHAR_HEIGHT && y < (BUFFER_ROWS + 1) * CHAR_HEIGHT)
                {
                    char temp_str[2] = {active_tab->text_buffer[row][col], '\0'};
                    XDrawString(display, window, gc, x, y, temp_str, 1);
                }
            }
        }
    }

    // Draw cursor
    int cursor_x = active_tab->cursor_col * CHAR_WIDTH;
    int cursor_y = (active_tab->cursor_row + 1) * CHAR_HEIGHT;

    // Ensure cursor is within bounds
    if (cursor_x >= 0 && cursor_x < BUFFER_COLS * CHAR_WIDTH &&
        cursor_y >= CHAR_HEIGHT && cursor_y < (BUFFER_ROWS + 1) * CHAR_HEIGHT)
    {
        XDrawString(display, window, gc, cursor_x, cursor_y, "_", 1);
    }
}

// Function to cleanup multiWatch processes and files
void cleanup_multiwatch()
{
    printf("Cleaning up multiWatch processes\n");
    
    for (int i = 0; i < multiwatch_count; i++) {
        if (multiwatch_processes[i].active) {
            printf("Killing process %d\n", multiwatch_processes[i].pid);
            
            // Kill the process
            kill(multiwatch_processes[i].pid, SIGTERM);
            
            // Wait for it to terminate
            int status;
            pid_t result = waitpid(multiwatch_processes[i].pid, &status, WNOHANG);
            if (result == 0) {
                // Process still running, force kill
                usleep(100000); // 100ms
                kill(multiwatch_processes[i].pid, SIGKILL);
                waitpid(multiwatch_processes[i].pid, &status, 0);
            }
            
            // Close file descriptor
            if (multiwatch_processes[i].fd != -1) {
                close(multiwatch_processes[i].fd);
            }
            
            // Remove temporary file
            if (unlink(multiwatch_processes[i].temp_file) == 0) {
                printf("Removed temp file: %s\n", multiwatch_processes[i].temp_file);
            } else {
                printf("Failed to remove temp file: %s\n", multiwatch_processes[i].temp_file);
            }
            
            multiwatch_processes[i].active = 0;
        }
    }
    multiwatch_count = 0;
    printf("Cleanup completed\n");
}

// Function to monitor multiWatch processes
void monitor_multiwatch_processes(Display *display, Window window, GC gc, Tab *tab)
{
    struct pollfd fds[MAX_MULTIWATCH_COMMANDS];
    char buffer[MULTIWATCH_BUFFER_SIZE];
    int active_count = multiwatch_count;
    int max_attempts = 50; // Try to read output for up to 5 seconds
    int attempts = 0;
    
    printf("Starting to monitor %d processes\n", active_count);
    
    while ((active_count > 0 || attempts < max_attempts) && multiwatch_mode) {
        // Initialize poll structures
        int poll_count = 0;
        for (int i = 0; i < multiwatch_count; i++) {
            if (multiwatch_processes[i].active && multiwatch_processes[i].fd != -1) {
                fds[poll_count].fd = multiwatch_processes[i].fd;
                fds[poll_count].events = POLLIN;
                fds[poll_count].revents = 0;
                poll_count++;
            }
        }
        
        // Always try to read from files, even if poll times out
        // This catches output from processes that finished quickly
        int data_read = 0;
        
        // Try to read from all file descriptors
        for (int i = 0; i < multiwatch_count; i++) {
            if (multiwatch_processes[i].active && multiwatch_processes[i].fd != -1) {
                ssize_t bytes_read = read(multiwatch_processes[i].fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    data_read = 1;
                    buffer[bytes_read] = '\0';
                    
                    // Add timestamp and command name
                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    char timestamp[64];
                    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
                    
                    // Split output by lines and add each line
                    char *line = buffer;
                    char *newline;
                    
                    do {
                        newline = strchr(line, '\n');
                        if (newline) {
                            *newline = '\0';
                        }
                        
                        if (strlen(line) > 0) {
                            char output_line[BUFFER_COLS];
                            snprintf(output_line, sizeof(output_line), "[%s] %s: %s", 
                                    timestamp, multiwatch_processes[i].command, line);
                            
                            add_text_to_buffer(tab, output_line);
                            printf("Output: %s\n", output_line);
                        }
                        
                        if (newline) {
                            line = newline + 1;
                        }
                    } while (newline);
                    
                    draw_text_buffer(display, window, gc);
                } else if (bytes_read == 0) {
                    // EOF reached
                    printf("EOF for process %d\n", multiwatch_processes[i].pid);
                    close(multiwatch_processes[i].fd);
                    multiwatch_processes[i].fd = -1;
                } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    // Read error
                    printf("Read error for process %d: %s\n", multiwatch_processes[i].pid, strerror(errno));
                    close(multiwatch_processes[i].fd);
                    multiwatch_processes[i].fd = -1;
                }
            }
        }
        
        // Also use poll for efficient waiting when no immediate data
        if (poll_count > 0 && !data_read) {
            int ret = poll(fds, poll_count, 100); // 100ms timeout
            
            if (ret > 0) {
                // Check for data on file descriptors that poll indicated are ready
                int fd_index = 0;
                for (int i = 0; i < multiwatch_count; i++) {
                    if (multiwatch_processes[i].active && multiwatch_processes[i].fd != -1) {
                        if (fds[fd_index].revents & POLLIN) {
                            ssize_t bytes_read = read(multiwatch_processes[i].fd, buffer, sizeof(buffer) - 1);
                            if (bytes_read > 0) {
                                buffer[bytes_read] = '\0';
                                
                                // Add timestamp and command name
                                time_t now = time(NULL);
                                struct tm *tm_info = localtime(&now);
                                char timestamp[64];
                                strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
                                
                                // Split output by lines and add each line
                                char *line = buffer;
                                char *newline;
                                
                                do {
                                    newline = strchr(line, '\n');
                                    if (newline) {
                                        *newline = '\0';
                                    }
                                    
                                    if (strlen(line) > 0) {
                                        char output_line[BUFFER_COLS];
                                        snprintf(output_line, sizeof(output_line), "[%s] %s: %s", 
                                                timestamp, multiwatch_processes[i].command, line);
                                        
                                        add_text_to_buffer(tab, output_line);
                                        printf("Poll Output: %s\n", output_line);
                                    }
                                    
                                    if (newline) {
                                        line = newline + 1;
                                    }
                                } while (newline);
                                
                                draw_text_buffer(display, window, gc);
                            }
                        }
                        fd_index++;
                    }
                }
            }
        }
        
        // Check process status and update active_count
        active_count = 0;
        for (int i = 0; i < multiwatch_count; i++) {
            if (multiwatch_processes[i].active) {
                int status;
                pid_t result = waitpid(multiwatch_processes[i].pid, &status, WNOHANG);
                if (result == multiwatch_processes[i].pid) {
                    // Process finished
                    printf("Process %d finished with status %d\n", multiwatch_processes[i].pid, WEXITSTATUS(status));
                    multiwatch_processes[i].active = 0;
                    if (multiwatch_processes[i].fd != -1) {
                        close(multiwatch_processes[i].fd);
                        multiwatch_processes[i].fd = -1;
                    }
                    
                    // Try to read any remaining output
                    char temp_buffer[1024];
                    int temp_fd = open(multiwatch_processes[i].temp_file, O_RDONLY);
                    if (temp_fd != -1) {
                        ssize_t final_read = read(temp_fd, temp_buffer, sizeof(temp_buffer) - 1);
                        if (final_read > 0) {
                            temp_buffer[final_read] = '\0';
                            time_t now = time(NULL);
                            struct tm *tm_info = localtime(&now);
                            char timestamp[64];
                            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
                            
                            char *line = temp_buffer;
                            char *newline;
                            do {
                                newline = strchr(line, '\n');
                                if (newline) *newline = '\0';
                                if (strlen(line) > 0) {
                                    char output_line[BUFFER_COLS];
                                    snprintf(output_line, sizeof(output_line), "[%s] %s: %s", 
                                            timestamp, multiwatch_processes[i].command, line);
                                    add_text_to_buffer(tab, output_line);
                                    printf("Final output: %s\n", output_line);
                                }
                                if (newline) line = newline + 1;
                            } while (newline);
                            draw_text_buffer(display, window, gc);
                        }
                        close(temp_fd);
                    }
                    
                    char done_msg[128];
                    snprintf(done_msg, sizeof(done_msg), 
                            "Command '%s' finished", multiwatch_processes[i].command);
                    add_text_to_buffer(tab, done_msg);
                    draw_text_buffer(display, window, gc);
                    
                } else if (result == 0) {
                    // Process still running
                    active_count++;
                } else {
                    // Error
                    printf("Error checking process %d: %s\n", multiwatch_processes[i].pid, strerror(errno));
                    multiwatch_processes[i].active = 0;
                    if (multiwatch_processes[i].fd != -1) {
                        close(multiwatch_processes[i].fd);
                        multiwatch_processes[i].fd = -1;
                    }
                }
            }
        }
        
        attempts++;
        
        // Check for user input or signals
        int has_events = 0;
        while (XPending(display) > 0 && !has_events) {
            XEvent ev;
            XNextEvent(display, &ev);
            
            if (ev.type == KeyPress) {
                KeySym keysym;
                char keybuf[256];
                XLookupString(&ev.xkey, keybuf, sizeof(keybuf) - 1, &keysym, NULL);
                
                if ((ev.xkey.state & ControlMask) && keysym == XK_c) {
                    printf("Ctrl+C detected in multiWatch\n");
                    add_text_to_buffer(tab, "Ctrl+C received - stopping multiWatch");
                    draw_text_buffer(display, window, gc);
                    cleanup_multiwatch();
                    multiwatch_mode = 0;
                    return;
                }
            }
            has_events = 1;
        }
        
        // Check for signals
        if (signal_received && which_signal == SIGINT) {
            printf("SIGINT received in multiWatch\n");
            signal_received = 0;
            which_signal = 0;
            add_text_to_buffer(tab, "SIGINT received - stopping multiWatch");
            draw_text_buffer(display, window, gc);
            cleanup_multiwatch();
            multiwatch_mode = 0;
            return;
        }
        
        // If no processes are active but we haven't tried enough times, keep trying to read
        if (active_count == 0 && attempts < max_attempts) {
            usleep(100000); // 100ms delay
        } else if (active_count == 0) {
            break; // No active processes and we've tried enough times
        } else {
            usleep(50000); // 50ms delay when processes are still active
        }
    }
    
    printf("multiWatch monitoring finished after %d attempts\n", attempts);
    cleanup_multiwatch();
    multiwatch_mode = 0;
    add_text_to_buffer(tab, "multiWatch completed");
    draw_text_buffer(display, window, gc);
}

// Function to handle multiWatch command
void handle_multiwatch_command(Display *display, Window window, GC gc, Tab *tab, const char *command)
{
    // Parse commands from: multiWatch "cmd1" "cmd2" "cmd3"
    char commands[MAX_MULTIWATCH_COMMANDS][MAX_COMMAND_LENGTH];
    int cmd_count = 0;
    
    char cmd_copy[MAX_COMMAND_LENGTH];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // Skip "multiWatch"
    char *ptr = cmd_copy + 10;
    
    // Parse quoted commands
    while (*ptr != '\0' && cmd_count < MAX_MULTIWATCH_COMMANDS) {
        // Skip whitespace
        while (*ptr == ' ') ptr++;
        
        if (*ptr == '"') {
            ptr++; // Skip opening quote
            char *start = ptr;
            
            // Find closing quote
            while (*ptr != '"' && *ptr != '\0') ptr++;
            
            if (*ptr == '"') {
                int len = ptr - start;
                if (len > 0 && len < MAX_COMMAND_LENGTH - 1) {
                    strncpy(commands[cmd_count], start, len);
                    commands[cmd_count][len] = '\0';
                    cmd_count++;
                }
                ptr++; // Skip closing quote
            }
        } else {
            break; // Invalid format
        }
    }
    
    if (cmd_count == 0) {
        add_text_to_buffer(tab, "Usage: multiWatch \"command1\" \"command2\" ...");
        draw_text_buffer(display, window, gc);
        return;
    }
    
    add_text_to_buffer(tab, "Starting multiWatch mode. Press Ctrl+C to stop.");
    draw_text_buffer(display, window, gc);
    
    // Initialize multiwatch processes
    multiwatch_count = cmd_count;
    multiwatch_mode = 1;
    
    // Create temporary files and fork processes
    for (int i = 0; i < cmd_count; i++) {
        // Create temporary file first
        snprintf(multiwatch_processes[i].temp_file, sizeof(multiwatch_processes[i].temp_file), 
                 ".temp.multiwatch.%d.%d.txt", getpid(), i);
        
        // Create the file and close it (child will reopen)
        int fd = open(multiwatch_processes[i].temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd != -1) {
            close(fd);
        }
        
        pid_t pid = fork();
        
        // In the child process section of handle_multiwatch_command:
if (pid == 0) {
    // Child process
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    
    // Redirect stdout to temporary file
    int out_fd = open(multiwatch_processes[i].temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1) {
        fprintf(stderr, "Failed to open temp file\n");
        exit(1);
    }
    
    dup2(out_fd, STDOUT_FILENO);
    dup2(out_fd, STDERR_FILENO);
    close(out_fd);
    
    // Parse and execute the command
    char *args[64];
    int arg_count = 0;
    char cmd_buffer[MAX_COMMAND_LENGTH];
    strncpy(cmd_buffer, commands[i], sizeof(cmd_buffer) - 1);
    cmd_buffer[sizeof(cmd_buffer) - 1] = '\0';
    
    // For commands with pipes, we need to handle them differently
    if (strstr(commands[i], "|") != NULL) {
        // Handle piped commands by executing through shell
        execl("/bin/sh", "sh", "-c", commands[i], NULL);
    } else {
        // Regular command - tokenize and execute directly
        char *token = strtok(cmd_buffer, " ");
        while (token != NULL && arg_count < 63) {
            args[arg_count++] = token;
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;
        
        if (arg_count == 0) {
            exit(1);
        }
        
        // Execute the command
        execvp(args[0], args);
    }
    
    // If we get here, exec failed
    fprintf(stderr, "Failed to execute: %s\n", commands[i]);
    exit(1);
}
        else if (pid > 0) {
            // Parent process
            multiwatch_processes[i].pid = pid;
            strncpy(multiwatch_processes[i].command, commands[i], MAX_COMMAND_LENGTH - 1);
            multiwatch_processes[i].active = 1;
            
            // Open the file for reading (non-blocking)
            multiwatch_processes[i].fd = open(multiwatch_processes[i].temp_file, O_RDONLY | O_NONBLOCK);
            if (multiwatch_processes[i].fd == -1) {
                printf("Failed to open temp file for reading: %s\n", multiwatch_processes[i].temp_file);
                multiwatch_processes[i].active = 0;
            }
            
            printf("Started process %d for: %s\n", pid, commands[i]);
        } else {
            // Fork failed
            multiwatch_processes[i].active = 0;
            printf("Fork failed for command: %s\n", commands[i]);
        }
    }
    
    // Start monitoring
    monitor_multiwatch_processes(display, window, gc, tab);
}

// Function to execute a command and capture its output
void execute_command(Display *display, Window window, GC gc, Tab *tab, const char *command)
{
    if (command == NULL || strlen(command) == 0)
    {
        add_text_to_buffer(tab, "");
        return;
    }

    // Check for multiWatch command first
    if (strncmp(command, "multiWatch", 10) == 0) {
        handle_multiwatch_command(display, window, gc, tab, command);
        return;
    }

    char command_copy[MAX_COMMAND_LENGTH];
    strncpy(command_copy, command, sizeof(command_copy) - 1);
    command_copy[sizeof(command_copy) - 1] = '\0';

    char *args[64];
    int arg_count = 0;
    char *token = strtok(command_copy, " ");
    while (token != NULL && arg_count < 63)
    {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

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
            snprintf(error_msg, sizeof(error_msg), "cd: %s: No such file or directory", path);
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
                add_text_to_buffer(tab, "Changed directory");
            }
        }
        handle_history_command(tab);
        return;
    }

    if (arg_count > 0 && strcmp(args[0], "history") == 0)
    {
        handle_history_command(tab);
        return;
    }

    int num_commands = 1;
    char *commands[16];
    char command_copy2[MAX_COMMAND_LENGTH];

    strncpy(command_copy2, command, sizeof(command_copy2) - 1);
    command_copy2[sizeof(command_copy2) - 1] = '\0';

    commands[0] = strtok(command_copy2, "|");
    while ((commands[num_commands] = strtok(NULL, "|")) != NULL && num_commands < 15)
    {
        num_commands++;
    }

    if (num_commands == 1)
    {
        int pipefd[2];
        pid_t pid;

        if (pipe(pipefd) == -1)
        {
            add_text_to_buffer(tab, "Error: Failed to create pipe");
            return;
        }

        pid = fork();

        if (pid == -1)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            add_text_to_buffer(tab, "Error: Fork failed");
            return;
        }
        else if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            char *args[64];
            int arg_count = 0;
            char *input_file = NULL;
            char *output_file = NULL;
            char cmd_copy[MAX_COMMAND_LENGTH];

            strncpy(cmd_copy, commands[0], sizeof(cmd_copy) - 1);
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

            if (input_file != NULL)
            {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1)
                {
                    fprintf(stderr, "Error: Cannot open input file '%s'\n", input_file);
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (output_file != NULL)
            {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1)
                {
                    fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            execvp(args[0], args);
            fprintf(stderr, "Error: Command not found: %s\n", args[0]);
            exit(1);
        }
        else
        {
            tab->foreground_pid = pid;

            int status;
            char buffer[1024];
            ssize_t bytes_read;
            char full_output[OUTPUT_BUFFER_SIZE] = {0};

            close(pipefd[1]);
            fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

            int child_exited = 0;
            while (!child_exited)
            {
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
                            kill(pid, SIGINT);
                            break;
                        }
                        else if ((ev.xkey.state & ControlMask) && keysym == XK_z)
                        {
                            printf("\nCtrl+Z detected - stopping process\n");
                            kill(pid, SIGTSTP);
                            break;
                        }
                    }
                }

                if (signal_received)
                {
                    signal_received = 0;
                    if (which_signal == SIGINT)
                    {
                        printf("\nCtrl+C received - interrupting process\n");
                        kill(pid, SIGINT);
                        break;
                    }
                    else if (which_signal == SIGTSTP)
                    {
                        printf("\nCtrl+Z received - stopping process\n");
                        kill(pid, SIGTSTP);
                        break;
                    }
                    which_signal = 0;
                }

                bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0)
                {
                    buffer[bytes_read] = '\0';
                    strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
                }

                int wait_result = waitpid(pid, &status, WNOHANG);
                if (wait_result == pid)
                {
                    child_exited = 1;
                }
                else if (wait_result == -1)
                {
                    break;
                }

                usleep(10000);
            }

            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
            {
                buffer[bytes_read] = '\0';
                strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
            }

            close(pipefd[0]);

            tab->foreground_pid = -1;

            if (strlen(full_output) > 0)
            {
                add_text_to_buffer(tab, full_output);
            }
            else
            {
                add_text_to_buffer(tab, "");
            }
        }
    }
    else
    {
        int pipefds[2][2];
        pid_t pids[16];
        int i;
        int final_output_pipe[2];

        if (pipe(final_output_pipe) == -1)
        {
            add_text_to_buffer(tab, "Error: Failed to create output pipe");
            return;
        }

        for (i = 0; i < num_commands - 1; i++)
        {
            if (pipe(pipefds[i % 2]) == -1)
            {
                add_text_to_buffer(tab, "Error: Failed to create pipe");
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);
                return;
            }
        }

        for (i = 0; i < num_commands; i++)
        {
            pids[i] = fork();

            if (pids[i] == -1)
            {
                add_text_to_buffer(tab, "Error: Fork failed");
                return;
            }
            else if (pids[i] == 0)
            {
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                if (i > 0)
                {
                    dup2(pipefds[(i - 1) % 2][0], STDIN_FILENO);
                }

                if (i < num_commands - 1)
                {
                    dup2(pipefds[i % 2][1], STDOUT_FILENO);
                    dup2(pipefds[i % 2][1], STDERR_FILENO);
                }
                else
                {
                    dup2(final_output_pipe[1], STDOUT_FILENO);
                    dup2(final_output_pipe[1], STDERR_FILENO);
                }

                for (int j = 0; j < num_commands - 1; j++)
                {
                    close(pipefds[j % 2][0]);
                    close(pipefds[j % 2][1]);
                }
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);

                char *args[64];
                int arg_count = 0;
                char cmd_copy[MAX_COMMAND_LENGTH];

                char *cmd = commands[i];
                char *end;
                while (*cmd == ' ')
                    cmd++;
                end = cmd + strlen(cmd) - 1;
                while (end > cmd && *end == ' ')
                {
                    *end = '\0';
                    end--;
                }

                strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
                cmd_copy[sizeof(cmd_copy) - 1] = '\0';

                char *token = strtok(cmd_copy, " ");
                while (token != NULL && arg_count < 63)
                {
                    args[arg_count++] = token;
                    token = strtok(NULL, " ");
                }
                args[arg_count] = NULL;

                execvp(args[0], args);
                fprintf(stderr, "Error: Command not found: %s\n", args[0]);
                exit(1);
            }
        }

        for (i = 0; i < num_commands - 1; i++)
        {
            close(pipefds[i % 2][0]);
            close(pipefds[i % 2][1]);
        }
        close(final_output_pipe[1]);

        tab->foreground_pid = pids[num_commands - 1];

        int status;
        char buffer[1024];
        ssize_t bytes_read;
        char full_output[OUTPUT_BUFFER_SIZE] = {0};

        fcntl(final_output_pipe[0], F_SETFL, O_NONBLOCK);

        int all_children_exited = 0;
        while (!all_children_exited)
        {
            bytes_read = read(final_output_pipe[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
            }

            all_children_exited = 1;
            for (i = 0; i < num_commands; i++)
            {
                if (waitpid(pids[i], &status, WNOHANG) == 0)
                {
                    all_children_exited = 0;
                }
            }

            usleep(10000);
        }

        while ((bytes_read = read(final_output_pipe[0], buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[bytes_read] = '\0';
            strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
        }

        close(final_output_pipe[0]);

        for (i = 0; i < num_commands; i++)
        {
            waitpid(pids[i], NULL, 0);
        }

        tab->foreground_pid = -1;

        if (strlen(full_output) > 0)
        {
            add_text_to_buffer(tab, full_output);
        }
        else
        {
            add_text_to_buffer(tab, "");
        }
    }
}

// Function to handle Enter key - executes command and shows output
void handle_enter_key(Display *display, Window window, GC gc, Tab *tab)
{
    if (strlen(tab->current_command) > 0)
    {
        add_to_history(tab, tab->current_command);
    }
    printf("ENTER pressed in tab %s - executing command: '%s'\n", tab->tab_name, tab->current_command);

    tab->text_buffer[tab->cursor_row][tab->cursor_col] = ' ';

    if (tab->cursor_row >= BUFFER_ROWS - 1)
    {
        scroll_buffer(tab);
    }
    else
    {
        tab->cursor_row++;
    }
    tab->cursor_col = 0;

    if (strlen(tab->current_command) > 0)
    {
        execute_command(display, window, gc, tab, tab->current_command);
    }
    else
    {
        add_text_to_buffer(tab, "");
    }

    if (tab->cursor_row >= BUFFER_ROWS - 1)
    {
        scroll_buffer(tab);
    }
    else
    {
        tab->cursor_row++;
        tab->cursor_col = 0;
    }

    tab->text_buffer[tab->cursor_row][0] = '>';
    tab->text_buffer[tab->cursor_row][1] = ' ';
    tab->cursor_col = 2;

    memset(tab->current_command, 0, MAX_COMMAND_LENGTH);
    tab->command_length = 0;
    tab->cursor_buffer_pos = 0;

    draw_text_buffer(display, window, gc);
}

// Function to handle keyboard input
void handle_keypress(Display *display, Window window, GC gc, XKeyEvent *key_event)
{
    Tab *active_tab = &tabs[active_tab_index];

    char buffer[256];
    KeySym keysym;
    int buffer_len;
    int shift_pressed = (key_event->state & ShiftMask);
    int control_pressed = (key_event->state & ControlMask);

    buffer_len = XLookupString(key_event, buffer, sizeof(buffer) - 1, &keysym, NULL);
    buffer[buffer_len] = '\0';

    switch (keysym)
    {
    case XK_Escape:
        if (active_tab->search_mode)
        {
            active_tab->search_mode = 0;
            memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH);
            active_tab->command_length = 0;
            active_tab->cursor_buffer_pos = 0;
            update_command_display(active_tab);
        }
        else
        {
            printf("ESC pressed - exiting\n");
            exit(0);
        }
        break;

    case XK_Return:
    case XK_KP_Enter:
        if (active_tab->search_mode)
        {
            active_tab->search_mode = 0;
            if (active_tab->search_pos > 0)
            {
                char found_command[MAX_COMMAND_LENGTH];
                if (search_history(active_tab, active_tab->search_buffer, found_command))
                {
                    strcpy(active_tab->current_command, found_command);
                    active_tab->command_length = strlen(active_tab->current_command);
                    active_tab->cursor_buffer_pos = active_tab->command_length;
                }
            }
            update_command_display(active_tab);
        }
        else
        {
            handle_enter_key(display, window, gc, active_tab);
        }
        break;
        // In the handle_keypress function, add this case:
    case XK_n:
        if (control_pressed)
        {
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
        }
        break;
        // Updated case for Tab key in handle_keypress function:
    case XK_Tab:
        if (control_pressed)
        {
            // Safety checks
            if (tab_count > 0 && tab_count <= MAX_TABS)
            {
                // Deactivate current tab
                if (active_tab_index >= 0 && active_tab_index < MAX_TABS)
                {
                    tabs[active_tab_index].active = 0;
                }
                // Move to next tab
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
            handle_tab_completion(active_tab);
        }
        break;

    // Add to handle_keypress:
    case XK_w:
        if (control_pressed)
        {
            close_current_tab();
            draw_text_buffer(display, window, gc);
            break;
        }
        goto default_case;

    case XK_BackSpace:
    case XK_Delete:
        if (active_tab->search_mode)
        {
            if (active_tab->search_pos > 0)
            {
                active_tab->search_pos--;
                active_tab->search_buffer[active_tab->search_pos] = '\0';
                update_command_display_with_prompt(active_tab, "(reverse-i-search)`': ");
            }
        }
        else
        {
            if (active_tab->cursor_buffer_pos > 0)
            {
                for (int i = active_tab->cursor_buffer_pos - 1; i < active_tab->command_length; i++)
                {
                    active_tab->current_command[i] = active_tab->current_command[i + 1];
                }
                active_tab->command_length--;
                active_tab->cursor_buffer_pos--;
                update_command_display(active_tab);
            }
        }
        break;

    case XK_Left:
        if (!active_tab->search_mode)
        {
            if (active_tab->cursor_buffer_pos > 0)
            {
                active_tab->cursor_buffer_pos--;
                update_command_display(active_tab);
            }
        }
        break;

    case XK_Right:
        if (!active_tab->search_mode)
        {
            if (active_tab->cursor_buffer_pos < active_tab->command_length)
            {
                active_tab->cursor_buffer_pos++;
                update_command_display(active_tab);
            }
        }
        break;

    case XK_Up:
        if (!active_tab->search_mode)
        {
            if (active_tab->history_current > 0)
            {
                active_tab->history_current--;
                strcpy(active_tab->current_command, active_tab->command_history[active_tab->history_current]);
                active_tab->command_length = strlen(active_tab->current_command);
                active_tab->cursor_buffer_pos = active_tab->command_length;
                update_command_display(active_tab);
            }
        }
        break;

    case XK_Down:
        if (!active_tab->search_mode)
        {
            if (active_tab->history_current < active_tab->history_count - 1)
            {
                active_tab->history_current++;
                strcpy(active_tab->current_command, active_tab->command_history[active_tab->history_current]);
                active_tab->command_length = strlen(active_tab->current_command);
                active_tab->cursor_buffer_pos = active_tab->command_length;
            }
            else if (active_tab->history_current == active_tab->history_count - 1)
            {
                active_tab->history_current = active_tab->history_count;
                memset(active_tab->current_command, 0, MAX_COMMAND_LENGTH);
                active_tab->command_length = 0;
                active_tab->cursor_buffer_pos = 0;
            }
            update_command_display(active_tab);
        }
        break;

    case XK_Home:
    case XK_a:
        if (control_pressed && !active_tab->search_mode)
        {
            active_tab->cursor_buffer_pos = 0;
            update_command_display(active_tab);
            break;
        }
        goto default_case;

    case XK_End:
    case XK_e:
        if (control_pressed && !active_tab->search_mode)
        {
            active_tab->cursor_buffer_pos = active_tab->command_length;
            update_command_display(active_tab);
            break;
        }
        goto default_case;

    case XK_r:
        if (control_pressed && !active_tab->search_mode)
        {
            enter_search_mode(active_tab);
            break;
        }
        goto default_case;

    case XK_space:
        if (!active_tab->search_mode)
        {
            if (active_tab->command_length < MAX_COMMAND_LENGTH - 1)
            {
                for (int i = active_tab->command_length; i > active_tab->cursor_buffer_pos; i--)
                {
                    active_tab->current_command[i] = active_tab->current_command[i - 1];
                }
                active_tab->current_command[active_tab->cursor_buffer_pos] = ' ';
                active_tab->command_length++;
                active_tab->current_command[active_tab->command_length] = '\0';
                active_tab->cursor_buffer_pos++;
                update_command_display(active_tab);
            }
        }
        break;

    default_case:
    default:
        if (active_tab->search_mode)
        {
            if (buffer_len > 0 && buffer[0] >= 32 && buffer[0] <= 126)
            {
                if (active_tab->search_pos < MAX_COMMAND_LENGTH - 1)
                {
                    active_tab->search_buffer[active_tab->search_pos] = buffer[0];
                    active_tab->search_pos++;
                    active_tab->search_buffer[active_tab->search_pos] = '\0';
                    update_command_display_with_prompt(active_tab, "(reverse-i-search)`': ");
                }
            }
        }
        else
        {
            if (buffer_len > 0 && buffer[0] >= 32 && buffer[0] <= 126 && !control_pressed)
            {
                if (active_tab->command_length < MAX_COMMAND_LENGTH - 1)
                {
                    for (int i = active_tab->command_length; i > active_tab->cursor_buffer_pos; i--)
                    {
                        active_tab->current_command[i] = active_tab->current_command[i - 1];
                    }
                    active_tab->current_command[active_tab->cursor_buffer_pos] = buffer[0];
                    active_tab->command_length++;
                    active_tab->current_command[active_tab->command_length] = '\0';
                    active_tab->cursor_buffer_pos++;
                    update_command_display(active_tab);

                    printf("Current command: '%s', cursor at: %d\n", active_tab->current_command, active_tab->cursor_buffer_pos);
                }
            }
        }
        break;
    }

    if (keysym != XK_Return && keysym != XK_KP_Enter)
    {
        draw_text_buffer(display, window, gc);
    }
}

// Updated signal handlers
void handle_sigint(int sig)
{
    signal_received = 1;
    which_signal = SIGINT;

     // If in multiwatch mode, stop it
    if (multiwatch_mode) {
        cleanup_multiwatch();
        multiwatch_mode = 0;
    }
}

void handle_sigtstp(int sig)
{
    signal_received = 1;
    which_signal = SIGTSTP;
}

int main()
{
    Display *display;
    Window window;
    XEvent event;
    int screen;
    GC gc;

    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);

    initialize_text_buffer();

    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(display);

    int win_width = BUFFER_COLS * CHAR_WIDTH;
    int win_height = BUFFER_ROWS * CHAR_HEIGHT;

    window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        100, 100,
        win_width,
        win_height,
        2,
        BlackPixel(display, screen),
        WhitePixel(display, screen));

    gc = XCreateGC(display, window, 0, NULL);
    XSetForeground(display, gc, BlackPixel(display, screen));

    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | StructureNotifyMask);

    XMapWindow(display, window);

    XStoreName(display, window, "X11 Shell with Tabs");

    printf("Shell terminal with tabs created!\n");
    printf("Shortcuts: Ctrl+N=new tab, Ctrl+W=close tab, Ctrl+Tab=switch tabs\n");
    printf("           Ctrl+R=search history, Ctrl+C=interrupt, Ctrl+Z=stop\n");
    printf("           Click tab headers to switch, Press ESC to exit\n");

    while (1)
    {
        if (XPending(display) > 0)
        {
            XNextEvent(display, &event);

            switch (event.type)
            {
            case Expose:
                printf("Expose event - redrawing text buffer\n");
                draw_text_buffer(display, window, gc);
                break;

            case KeyPress:
                handle_keypress(display, window, gc, &event.xkey);
                break;

            case ButtonPress:
                if (event.xbutton.y < CHAR_HEIGHT)
                {
                    handle_tab_click(event.xbutton.x / CHAR_WIDTH);
                    draw_text_buffer(display, window, gc);
                }
                else
                {
                    printf("ButtonPress event received - focusing on window\n");
                    XSetInputFocus(display, window, RevertToParent, CurrentTime);
                }
                break;

            case ConfigureNotify:
                break;
            }
        }

        if (signal_received)
        {
            signal_received = 0;

            if (which_signal == SIGINT)
            {
                printf("\nCtrl+C received in shell\n");
                Tab *active_tab = &tabs[active_tab_index];
                if (active_tab->foreground_pid > 0)
                {
                    kill(active_tab->foreground_pid, SIGINT);
                    printf("Sent SIGINT to process %d\n", active_tab->foreground_pid);
                    active_tab->foreground_pid = -1;
                }
            }
            else if (which_signal == SIGTSTP)
            {
                printf("\nCtrl+Z received\n");
                Tab *active_tab = &tabs[active_tab_index];
                if (active_tab->foreground_pid > 0)
                {
                    kill(active_tab->foreground_pid, SIGSTOP);
                    printf("Stopped process %d\n", active_tab->foreground_pid);
                    active_tab->foreground_pid = -1;
                }
            }
            which_signal = 0;
        }

        usleep(10000);
    }

    return 0;
}