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

#define BUFFER_ROWS 25
#define BUFFER_COLS 80
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16
#define MAX_COMMAND_LENGTH 256
#define OUTPUT_BUFFER_SIZE 4096

// Global text buffer
char text_buffer[BUFFER_ROWS][BUFFER_COLS];
char current_command[MAX_COMMAND_LENGTH];
int command_length = 0;
int cursor_row = 1;
int cursor_col = 0;
int cursor_buffer_pos = 0; // Cursor position in command buffer
pid_t foreground_pid = -1;
volatile sig_atomic_t signal_received = 0;
volatile sig_atomic_t which_signal = 0;

#define MAX_HISTORY_SIZE 10000
char command_history[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH];
int history_count = 0;
int history_current = -1;
int search_mode = 0;
char search_buffer[MAX_COMMAND_LENGTH] = {0};
int search_pos = 0;

// Function to add a command to history
void add_to_history(const char *command)
{
    // Skip empty commands and duplicates of last command
    if (strlen(command) == 0 ||
        (history_count > 0 && strcmp(command_history[history_count - 1], command) == 0))
    {
        return;
    }

    if (history_count < MAX_HISTORY_SIZE)
    {
        strncpy(command_history[history_count], command, MAX_COMMAND_LENGTH - 1);
        command_history[history_count][MAX_COMMAND_LENGTH - 1] = '\0';
        history_count++;
    }
    else
    {
        // Shift history down (remove oldest)
        for (int i = 1; i < MAX_HISTORY_SIZE; i++)
        {
            strcpy(command_history[i - 1], command_history[i]);
        }
        strncpy(command_history[MAX_HISTORY_SIZE - 1], command, MAX_COMMAND_LENGTH - 1);
        command_history[MAX_HISTORY_SIZE - 1][MAX_COMMAND_LENGTH - 1] = '\0';
    }
    history_current = history_count;
}

// Function to handle history built-in command
void handle_history_command()
{
    int start = (history_count > 10) ? history_count - 10 : 0;
    for (int i = start; i < history_count; i++)
    {
        char history_line[256];
        snprintf(history_line, sizeof(history_line), "%d: %s", i + 1, command_history[i]);
        add_text_to_buffer(history_line);
    }
}

// Function to search history
int search_history(const char *search_term, char *result)
{
    // Search from most recent to oldest
    for (int i = history_count - 1; i >= 0; i--)
    {
        if (strstr(command_history[i], search_term) != NULL)
        {
            strcpy(result, command_history[i]);
            return 1;
        }
    }
    return 0; // Not found
}

// Function to enter search mode
void enter_search_mode()
{
    search_mode = 1;
    search_pos = 0;
    search_buffer[0] = '\0';

    // Clear current command and show search prompt
    memset(current_command, 0, MAX_COMMAND_LENGTH);
    command_length = 0;
    cursor_buffer_pos = 0;

    // Update display to show search prompt
    update_command_display_with_prompt("(reverse-i-search)`': ");
}

// Function to update display with custom prompt
void update_command_display_with_prompt(const char *prompt)
{
    // Clear the current command line in text buffer
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        text_buffer[cursor_row][col] = ' ';
    }

    // Write the custom prompt
    int prompt_len = strlen(prompt);
    for (int i = 0; i < prompt_len && i < BUFFER_COLS; i++)
    {
        text_buffer[cursor_row][i] = prompt[i];
    }

    // Write the search buffer
    int display_col = prompt_len;
    for (int i = 0; i < search_pos && display_col < BUFFER_COLS; i++)
    {
        text_buffer[cursor_row][display_col] = search_buffer[i];
        display_col++;
    }

    // Update the visual cursor position
    cursor_col = prompt_len + search_pos;
}

// Function to scroll the entire buffer up by one line
void scroll_buffer()
{
    // Move all lines up by one (losing the top line)
    for (int row = 0; row < BUFFER_ROWS - 1; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            text_buffer[row][col] = text_buffer[row + 1][col];
        }
    }

    // Clear the bottom line
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        text_buffer[BUFFER_ROWS - 1][col] = ' ';
    }

    // Update cursor position to stay on the last line
    cursor_row = BUFFER_ROWS - 1;
}

// Function to initialize the text buffer with some content
void initialize_text_buffer()
{
    cursor_buffer_pos = 0; // Start at beginning of line
    // Clear the buffer with spaces
    for (int row = 0; row < BUFFER_ROWS; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            text_buffer[row][col] = ' ';
        }
    }

    // Add some sample text
    char *welcome_message = "Welcome to X11 Shell Terminal!";
    char *instructions = "Type commands like 'ls' or 'pwd' and press ENTER";

    // Center the welcome message on row 1
    int welcome_len = strlen(welcome_message);
    int welcome_start = (BUFFER_COLS - welcome_len) / 2;
    for (int i = 0; i < welcome_len && welcome_start + i < BUFFER_COLS; i++)
    {
        text_buffer[1][welcome_start + i] = welcome_message[i];
    }

    // Center instructions on row 3
    int instr_len = strlen(instructions);
    int instr_start = (BUFFER_COLS - instr_len) / 2;
    for (int i = 0; i < instr_len && instr_start + i < BUFFER_COLS; i++)
    {
        text_buffer[3][instr_start + i] = instructions[i];
    }

    // Add a command prompt at the bottom
    text_buffer[BUFFER_ROWS - 1][0] = '>';
    text_buffer[BUFFER_ROWS - 1][1] = ' ';

    // Set initial cursor position after prompt (on the last line)
    cursor_row = BUFFER_ROWS - 1;
    cursor_col = 2;

    // Initialize command buffer
    memset(current_command, 0, MAX_COMMAND_LENGTH);
    command_length = 0;
}

// Function to draw the entire text buffer to the window
void draw_text_buffer(Display *display, Window window, GC gc)
{
    // Clear the window
    XClearWindow(display, window);

    // Draw each character from the buffer
    for (int row = 0; row < BUFFER_ROWS; row++)
    {
        for (int col = 0; col < BUFFER_COLS; col++)
        {
            if (text_buffer[row][col] != ' ')
            {
                // Calculate position for this character
                int x = col * CHAR_WIDTH;
                int y = (row + 1) * CHAR_HEIGHT;

                // Create a temporary string for this character
                char temp_str[2] = {text_buffer[row][col], '\0'};

                // Draw the character
                XDrawString(display, window, gc, x, y, temp_str, 1);
            }
        }
    }

    // Draw cursor (simple underscore at current position)
    int cursor_x = cursor_col * CHAR_WIDTH;
    int cursor_y = (cursor_row + 1) * CHAR_HEIGHT;
    XDrawString(display, window, gc, cursor_x, cursor_y, "_", 1);
}

// Function to add text to the buffer, handling line breaks and scrolling
void add_text_to_buffer(const char *text)
{
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
            *newline = '\0'; // Temporarily terminate at newline
        }

        // Scroll if we're at the bottom
        if (cursor_row >= BUFFER_ROWS - 1)
        {
            scroll_buffer();
        }
        else
        {
            cursor_row++;
        }
        cursor_col = 0;

        // Copy the line to the buffer
        int line_len = strlen(line);
        int max_len = BUFFER_COLS - 1;
        int copy_len = line_len < max_len ? line_len : max_len;

        for (int i = 0; i < copy_len; i++)
        {
            text_buffer[cursor_row][i] = line[i];
        }

        if (newline)
        {
            line = newline + 1; // Move to next line
        }
    } while (newline);
}

// Function to execute a command and capture its output
void execute_command(Display *display, Window window, GC gc, const char *command)
{
    // Skip empty commands
    if (command == NULL || strlen(command) == 0)
    {
        add_text_to_buffer("");
        return;
    }

    // Check for built-in cd command
    char command_copy[MAX_COMMAND_LENGTH];
    strncpy(command_copy, command, sizeof(command_copy) - 1);
    command_copy[sizeof(command_copy) - 1] = '\0';

    // Parse the command to check if it's cd
    char *args[64];
    int arg_count = 0;
    char *token = strtok(command_copy, " ");
    while (token != NULL && arg_count < 63)
    {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    // Handle built-in cd command (no forking)
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
            add_text_to_buffer(error_msg);
        }
        else
        {
            // Optional: Show new directory
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
            {
                char success_msg[256];
                snprintf(success_msg, sizeof(success_msg), "Changed to directory: %s", cwd);
                add_text_to_buffer(success_msg);
            }
            else
            {
                add_text_to_buffer("Changed directory");
            }
        }
        handle_history_command();
        return; // Important: Return without forking
    }

    // Check if this is a piped command
    int num_commands = 1;
    char *commands[16]; // Max 16 commands in pipeline
    char command_copy2[MAX_COMMAND_LENGTH];

    strncpy(command_copy2, command, sizeof(command_copy2) - 1);
    command_copy2[sizeof(command_copy2) - 1] = '\0';

    // Split commands by pipe symbol
    commands[0] = strtok(command_copy2, "|");
    while ((commands[num_commands] = strtok(NULL, "|")) != NULL && num_commands < 15)
    {
        num_commands++;
    }

    // If no pipes, use the original single command logic
    if (num_commands == 1)
    {
        // Single command - use existing logic with redirections
        int pipefd[2];
        pid_t pid;

        if (pipe(pipefd) == -1)
        {
            add_text_to_buffer("Error: Failed to create pipe");
            return;
        }

        pid = fork();

        if (pid == -1)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            add_text_to_buffer("Error: Fork failed");
            return;
        }
        else if (pid == 0)
        {
            // Child process for single command

            // Reset signals to default so child can be killed by Ctrl+C
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            // Parse for redirections in single command
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
            // Parent process for single command
            foreground_pid = pid; // Set as foreground process

            int status;
            char buffer[1024];
            ssize_t bytes_read;
            char full_output[OUTPUT_BUFFER_SIZE] = {0};

            close(pipefd[1]);
            fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

            int child_exited = 0;
            while (!child_exited)
            {
                // Check for X events and signals while waiting
                if (XPending(display) > 0)
                {
                    XEvent ev;
                    XNextEvent(display, &ev);
                    if (ev.type == KeyPress)
                    {
                        // Handle interrupt keys directly
                        KeySym keysym;
                        char keybuf[256];
                        XLookupString(&ev.xkey, keybuf, sizeof(keybuf) - 1, &keysym, NULL);

                        // Check for Ctrl+C or Ctrl+Z
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

                // Check for signals
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

                // Try to read from pipe
                bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0)
                {
                    buffer[bytes_read] = '\0';
                    strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
                }

                // Check if child process has exited
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

            // Read any remaining data after child exit
            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
            {
                buffer[bytes_read] = '\0';
                strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
            }

            close(pipefd[0]);

            // Reset foreground pid after command finishes
            foreground_pid = -1;

            if (strlen(full_output) > 0)
            {
                add_text_to_buffer(full_output);
            }
            else
            {
                add_text_to_buffer("");
            }
        }
    }
    else
    {
        // Multiple commands with pipes
        int pipefds[2][2]; // Two sets of pipes for connecting commands
        pid_t pids[16];    // Store child PIDs
        int i;
        int final_output_pipe[2]; // Additional pipe to capture final output

        // Create pipe for final output capture
        if (pipe(final_output_pipe) == -1)
        {
            add_text_to_buffer("Error: Failed to create output pipe");
            return;
        }

        // Create pipes for command connections
        for (i = 0; i < num_commands - 1; i++)
        {
            if (pipe(pipefds[i % 2]) == -1)
            {
                add_text_to_buffer("Error: Failed to create pipe");
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);
                return;
            }
        }

        // Fork child processes
        for (i = 0; i < num_commands; i++)
        {
            pids[i] = fork();

            if (pids[i] == -1)
            {
                add_text_to_buffer("Error: Fork failed");
                return;
            }
            else if (pids[i] == 0)
            {
                // Child process

                // Reset signals to default
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                // Connect stdin to previous pipe (if not first command)
                if (i > 0)
                {
                    dup2(pipefds[(i - 1) % 2][0], STDIN_FILENO);
                }

                // Connect stdout and stderr to next pipe (if not last command)
                if (i < num_commands - 1)
                {
                    dup2(pipefds[i % 2][1], STDOUT_FILENO);
                    dup2(pipefds[i % 2][1], STDERR_FILENO);
                }
                else
                {
                    // Last command - redirect both stdout and stderr to our final output pipe
                    dup2(final_output_pipe[1], STDOUT_FILENO);
                    dup2(final_output_pipe[1], STDERR_FILENO);
                }

                // Close all pipe ends in child
                for (int j = 0; j < num_commands - 1; j++)
                {
                    close(pipefds[j % 2][0]);
                    close(pipefds[j % 2][1]);
                }
                close(final_output_pipe[0]);
                close(final_output_pipe[1]);

                // Parse and execute this command
                char *args[64];
                int arg_count = 0;
                char cmd_copy[MAX_COMMAND_LENGTH];

                // Trim whitespace from command - FIXED VERSION
                char *cmd = commands[i];
                char *end;
                while (*cmd == ' ')
                    cmd++; // Trim leading spaces
                // Trim trailing spaces
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

        // Parent process - close all pipe ends we don't need
        for (i = 0; i < num_commands - 1; i++)
        {
            close(pipefds[i % 2][0]);
            close(pipefds[i % 2][1]);
        }
        close(final_output_pipe[1]); // Close write end of final output pipe

        // Set last process as foreground for signal handling
        foreground_pid = pids[num_commands - 1];

        // Read the final output from the pipe
        int status;
        char buffer[1024];
        ssize_t bytes_read;
        char full_output[OUTPUT_BUFFER_SIZE] = {0};

        fcntl(final_output_pipe[0], F_SETFL, O_NONBLOCK);

        int all_children_exited = 0;
        while (!all_children_exited)
        {
            // Try to read from final output pipe
            bytes_read = read(final_output_pipe[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
            }

            // Check if all children have exited
            all_children_exited = 1;
            for (i = 0; i < num_commands; i++)
            {
                if (waitpid(pids[i], &status, WNOHANG) == 0)
                {
                    all_children_exited = 0; // This child is still running
                }
            }

            usleep(10000);
        }

        // Read any remaining data
        while ((bytes_read = read(final_output_pipe[0], buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[bytes_read] = '\0';
            strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
        }

        close(final_output_pipe[0]);

        // Wait for all children to finish completely
        for (i = 0; i < num_commands; i++)
        {
            waitpid(pids[i], NULL, 0);
        }

        // Reset foreground pid after all commands finish
        foreground_pid = -1;

        // Add the piped command output to our text buffer
        if (strlen(full_output) > 0)
        {
            add_text_to_buffer(full_output);
        }
        else
        {
            add_text_to_buffer("");
        }
    }
}

// Function to handle Enter key - executes command and shows output
void handle_enter_key(Display *display, Window window, GC gc)
{
    // In handle_enter_key, add this at the beginning:
    if (strlen(current_command) > 0)
    {
        add_to_history(current_command);
    }
    printf("ENTER pressed - executing command: '%s'\n", current_command);

    // Remove cursor from current position
    text_buffer[cursor_row][cursor_col] = ' ';

    // Show the command that was entered (echo)
    if (cursor_row >= BUFFER_ROWS - 1)
    {
        scroll_buffer();
    }
    else
    {
        cursor_row++;
    }
    cursor_col = 0;

    // Echo the command
    // char echo_message[BUFFER_COLS+10];
    // snprintf(echo_message, sizeof(echo_message), "> %s",BUFFER_COLS-3, current_command);
    // int echo_len = strlen(echo_message);
    // for (int i = 0; i < echo_len && i < BUFFER_COLS; i++) {
    //     text_buffer[cursor_row][i] = echo_message[i];
    // }

    // Execute the command and capture its output
    if (strlen(current_command) > 0)
    {
        execute_command(display, window, gc, current_command);
    }
    else
    {
        // Empty command - just add new prompt
        add_text_to_buffer("");
    }

    // Add new prompt
    if (cursor_row >= BUFFER_ROWS - 1)
    {
        scroll_buffer();
    }
    else
    {
        cursor_row++;
        cursor_col = 0;
    }

    text_buffer[cursor_row][0] = '>';
    text_buffer[cursor_row][1] = ' ';
    cursor_col = 2;

    // Clear command buffer for new input
    memset(current_command, 0, MAX_COMMAND_LENGTH);
    command_length = 0;
    cursor_buffer_pos = 0; // Reset cursor to beginning

    // Redraw the window
    draw_text_buffer(display, window, gc);
}

// Function to update the command display with proper cursor positioning
void update_command_display()
{
    // Clear the current command line in text buffer
    for (int col = 0; col < BUFFER_COLS; col++)
    {
        text_buffer[cursor_row][col] = ' ';
    }

    // Write the prompt
    text_buffer[cursor_row][0] = '>';
    text_buffer[cursor_row][1] = ' ';

    // Write the command text
    int display_col = 2;
    for (int i = 0; i < command_length && display_col < BUFFER_COLS; i++)
    {
        text_buffer[cursor_row][display_col] = current_command[i];
        display_col++;
    }

    // Update the visual cursor position
    cursor_col = 2 + cursor_buffer_pos;
}

// Function to handle keyboard input
void handle_keypress(Display *display, Window window, GC gc, XKeyEvent *key_event)
{
    char buffer[256];
    KeySym keysym;
    int buffer_len;
    int shift_pressed = (key_event->state & ShiftMask);
    int control_pressed = (key_event->state & ControlMask);

    // Convert keycode to string and keysym
    buffer_len = XLookupString(key_event, buffer, sizeof(buffer) - 1, &keysym, NULL);
    buffer[buffer_len] = '\0';

    // Handle special keys
    switch (keysym)
    {
    case XK_Escape:
        if (search_mode)
        {
            // Cancel search mode
            search_mode = 0;
            memset(current_command, 0, MAX_COMMAND_LENGTH);
            command_length = 0;
            cursor_buffer_pos = 0;
            update_command_display();
        }
        else
        {
            printf("ESC pressed - exiting\n");
            exit(0);
        }
        break;

    case XK_Return:
    case XK_KP_Enter:
        if (search_mode)
        {
            // Exit search mode and use found command
            search_mode = 0;
            if (search_pos > 0)
            {
                char found_command[MAX_COMMAND_LENGTH];
                if (search_history(search_buffer, found_command))
                {
                    strcpy(current_command, found_command);
                    command_length = strlen(current_command);
                    cursor_buffer_pos = command_length;
                }
            }
            update_command_display();
        }
        else
        {
            handle_enter_key(display, window, gc);
        }
        break;

    case XK_BackSpace:
    case XK_Delete:
        if (search_mode)
        {
            // Backspace in search mode
            if (search_pos > 0)
            {
                search_pos--;
                search_buffer[search_pos] = '\0';
                update_command_display_with_prompt("(reverse-i-search)`': ");
            }
        }
        else
        {
            // Handle backspace - UPDATED FOR CURSOR POSITION
            if (cursor_buffer_pos > 0)
            {
                // Shift all characters after cursor left by one
                for (int i = cursor_buffer_pos - 1; i < command_length; i++)
                {
                    current_command[i] = current_command[i + 1];
                }
                command_length--;
                cursor_buffer_pos--;

                // Update text buffer display
                update_command_display();
            }
        }
        break;

    case XK_Left:
        if (!search_mode)
        {
            // Move cursor left
            if (cursor_buffer_pos > 0)
            {
                cursor_buffer_pos--;
                update_command_display();
            }
        }
        break;

    case XK_Right:
        if (!search_mode)
        {
            // Move cursor right
            if (cursor_buffer_pos < command_length)
            {
                cursor_buffer_pos++;
                update_command_display();
            }
        }
        break;

    case XK_Up:
        if (!search_mode)
        {
            // Navigate history backwards
            if (history_current > 0)
            {
                history_current--;
                strcpy(current_command, command_history[history_current]);
                command_length = strlen(current_command);
                cursor_buffer_pos = command_length;
                update_command_display();
            }
        }
        break;

    case XK_Down:
        if (!search_mode)
        {
            // Navigate history forwards
            if (history_current < history_count - 1)
            {
                history_current++;
                strcpy(current_command, command_history[history_current]);
                command_length = strlen(current_command);
                cursor_buffer_pos = command_length;
            }
            else if (history_current == history_count - 1)
            {
                // Clear command when at bottom
                history_current = history_count;
                memset(current_command, 0, MAX_COMMAND_LENGTH);
                command_length = 0;
                cursor_buffer_pos = 0;
            }
            update_command_display();
        }
        break;

    case XK_Home:
    case XK_a:
        if (control_pressed && !search_mode)
        {
            // Ctrl+A - move to beginning of line
            cursor_buffer_pos = 0;
            update_command_display();
            break;
        }
        // Fall through for regular 'a' key
        goto default_case;

    case XK_End:
    case XK_e:
        if (control_pressed && !search_mode)
        {
            // Ctrl+E - move to end of line
            cursor_buffer_pos = command_length;
            update_command_display();
            break;
        }
        // Fall through for regular 'e' key
        goto default_case;

    case XK_r:
        if (control_pressed && !search_mode)
        {
            // Ctrl+R - enter search mode
            enter_search_mode();
            break;
        }
        // Fall through for regular 'r' key
        goto default_case;

    case XK_space:
        if (!search_mode)
        {
            // Handle space bar - UPDATED FOR CURSOR POSITION
            if (command_length < MAX_COMMAND_LENGTH - 1)
            {
                // Make space for new character
                for (int i = command_length; i > cursor_buffer_pos; i--)
                {
                    current_command[i] = current_command[i - 1];
                }
                current_command[cursor_buffer_pos] = ' ';
                command_length++;
                current_command[command_length] = '\0';
                cursor_buffer_pos++;
                update_command_display();
            }
        }
        break;

    default_case:
    default:
        if (search_mode)
        {
            // Handle search mode input
            if (buffer_len > 0 && buffer[0] >= 32 && buffer[0] <= 126)
            {
                // Add character to search
                if (search_pos < MAX_COMMAND_LENGTH - 1)
                {
                    search_buffer[search_pos] = buffer[0];
                    search_pos++;
                    search_buffer[search_pos] = '\0';
                    update_command_display_with_prompt("(reverse-i-search)`': ");
                }
            }
        }
        else
        {
            // Handle printable characters - UPDATED FOR CURSOR POSITION
            if (buffer_len > 0 && buffer[0] >= 32 && buffer[0] <= 126 && !control_pressed)
            {
                if (command_length < MAX_COMMAND_LENGTH - 1)
                {
                    // Make space for new character at cursor position
                    for (int i = command_length; i > cursor_buffer_pos; i--)
                    {
                        current_command[i] = current_command[i - 1];
                    }
                    current_command[cursor_buffer_pos] = buffer[0];
                    command_length++;
                    current_command[command_length] = '\0';
                    cursor_buffer_pos++;
                    update_command_display();

                    printf("Current command: '%s', cursor at: %d\n", current_command, cursor_buffer_pos);
                }
            }
        }
        break;
    }

    // Redraw the window to show changes
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
    GC gc; // Graphics Context

    // Set up signal handlers - USING signal() INSTEAD OF sigaction()
    // Handle SIGINT (Ctrl+C) - ignore in shell
    signal(SIGINT, handle_sigint);

    // Handle SIGTSTP (Ctrl+Z) - stop foreground process
    signal(SIGTSTP, handle_sigtstp);

    // Initialize the text buffer
    initialize_text_buffer();

    // 1. Open connection to the display
    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(display);

    // 2. Create a simple window - now sized for our text buffer
    int win_width = BUFFER_COLS * CHAR_WIDTH;
    int win_height = BUFFER_ROWS * CHAR_HEIGHT;

    window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        100, 100,                    // x, y position
        win_width,                   // width
        win_height,                  // height
        2,                           // border width
        BlackPixel(display, screen), // border color
        WhitePixel(display, screen)  // background color
    );

    // Create graphics context for drawing
    gc = XCreateGC(display, window, 0, NULL);
    XSetForeground(display, gc, BlackPixel(display, screen));

    // Select events - now including KeyPress for keyboard input
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | StructureNotifyMask);

    // 3. Map the window (make it visible)
    XMapWindow(display, window);

    // Set window title
    XStoreName(display, window, "X11 Shell with Signal Handling");

    printf("Shell terminal with signal handling created!\n");
    printf("Try Ctrl+C to interrupt commands, Ctrl+Z to stop commands\n");
    printf("Press ESC to exit\n");

    // 4. Main event loop - NON-BLOCKING VERSION
    while (1)
    {
        // Check if any events are pending without blocking
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
                printf("ButtonPress event received - focusing on window\n");
                XSetInputFocus(display, window, RevertToParent, CurrentTime);
                break;

            case ConfigureNotify:
                // Window resized or moved
                break;
            }
        }

        // Check for signals
        if (signal_received)
        {
            signal_received = 0;

            if (which_signal == SIGINT)
            {
                printf("\nCtrl+C received in shell\n");
                if (foreground_pid > 0)
                {
                    kill(foreground_pid, SIGINT);
                    printf("Sent SIGINT to process %d\n", foreground_pid);
                    foreground_pid = -1;
                }
            }
            else if (which_signal == SIGTSTP)
            {
                printf("\nCtrl+Z received\n");
                if (foreground_pid > 0)
                {
                    kill(foreground_pid, SIGSTOP);
                    printf("Stopped process %d\n", foreground_pid);
                    foreground_pid = -1;
                }
            }
            which_signal = 0;
        }

        // Small delay to prevent busy waiting
        usleep(10000); // 10ms
    }

    return 0;
}
