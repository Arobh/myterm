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

// Function to scroll the entire buffer up by one line
void scroll_buffer() {
    // Move all lines up by one (losing the top line)
    for (int row = 0; row < BUFFER_ROWS - 1; row++) {
        for (int col = 0; col < BUFFER_COLS; col++) {
            text_buffer[row][col] = text_buffer[row + 1][col];
        }
    }
    
    // Clear the bottom line
    for (int col = 0; col < BUFFER_COLS; col++) {
        text_buffer[BUFFER_ROWS - 1][col] = ' ';
    }
    
    // Update cursor position to stay on the last line
    cursor_row = BUFFER_ROWS - 1;
}

// Function to initialize the text buffer with some content
void initialize_text_buffer() {
    // Clear the buffer with spaces
    for (int row = 0; row < BUFFER_ROWS; row++) {
        for (int col = 0; col < BUFFER_COLS; col++) {
            text_buffer[row][col] = ' ';
        }
    }
    
    // Add some sample text
    char *welcome_message = "Welcome to X11 Shell Terminal!";
    char *instructions = "Type commands like 'ls' or 'pwd' and press ENTER";
    
    // Center the welcome message on row 1
    int welcome_len = strlen(welcome_message);
    int welcome_start = (BUFFER_COLS - welcome_len) / 2;
    for (int i = 0; i < welcome_len && welcome_start + i < BUFFER_COLS; i++) {
        text_buffer[1][welcome_start + i] = welcome_message[i];
    }
    
    // Center instructions on row 3
    int instr_len = strlen(instructions);
    int instr_start = (BUFFER_COLS - instr_len) / 2;
    for (int i = 0; i < instr_len && instr_start + i < BUFFER_COLS; i++) {
        text_buffer[3][instr_start + i] = instructions[i];
    }
    
    // Add a command prompt at the bottom
    text_buffer[BUFFER_ROWS-1][0] = '>';
    text_buffer[BUFFER_ROWS-1][1] = ' ';
    
    // Set initial cursor position after prompt (on the last line)
    cursor_row = BUFFER_ROWS - 1;
    cursor_col = 2;
    
    // Initialize command buffer
    memset(current_command, 0, MAX_COMMAND_LENGTH);
    command_length = 0;
}

// Function to draw the entire text buffer to the window
void draw_text_buffer(Display *display, Window window, GC gc) {
    // Clear the window
    XClearWindow(display, window);
    
    // Draw each character from the buffer
    for (int row = 0; row < BUFFER_ROWS; row++) {
        for (int col = 0; col < BUFFER_COLS; col++) {
            if (text_buffer[row][col] != ' ') {
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
void add_text_to_buffer(const char *text) {
    char output_buffer[OUTPUT_BUFFER_SIZE];
    strncpy(output_buffer, text, OUTPUT_BUFFER_SIZE - 1);
    output_buffer[OUTPUT_BUFFER_SIZE - 1] = '\0';
    
    char *line = output_buffer;
    char *newline;
    
    do {
        newline = strchr(line, '\n');
        if (newline) {
            *newline = '\0';  // Temporarily terminate at newline
        }
        
        // Scroll if we're at the bottom
        if (cursor_row >= BUFFER_ROWS - 1) {
            scroll_buffer();
        } else {
            cursor_row++;
        }
        cursor_col = 0;
        
        // Copy the line to the buffer
        int line_len = strlen(line);
        int max_len = BUFFER_COLS - 1;
        int copy_len = line_len < max_len ? line_len : max_len;
        
        for (int i = 0; i < copy_len; i++) {
            text_buffer[cursor_row][i] = line[i];
        }
        
        if (newline) {
            line = newline + 1;  // Move to next line
        }
    } while (newline);
}

// Function to execute a command and capture its output
void execute_command(Display *display, Window window, GC gc, const char *command) {
    printf("Executing command: '%s'\n", command);
    
    // Skip empty commands
    if (command == NULL || strlen(command) == 0) {
        printf("Empty command, skipping execution\n");
        add_text_to_buffer("");
        return;
    }
    
    int pipefd[2];
    pid_t pid;
    
    // Create pipe for capturing output
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        add_text_to_buffer("Error: Failed to create pipe");
        return;
    }
    
    pid = fork();
    
    if (pid == -1) {
        // Fork failed
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        add_text_to_buffer("Error: Fork failed");
        return;
    }
    else if (pid == 0) {
        // Child process
        
        // Close read end of pipe
        close(pipefd[0]);
        
        // Redirect stdout to write end of pipe
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);  // Also capture stderr
        close(pipefd[1]);
        
        // Simple command parsing - split by spaces
        char *args[64];
        int arg_count = 0;
        char command_copy[MAX_COMMAND_LENGTH];
        
        strncpy(command_copy, command, sizeof(command_copy) - 1);
        command_copy[sizeof(command_copy) - 1] = '\0';
        
        // Tokenize the command
        char *token = strtok(command_copy, " ");
        while (token != NULL && arg_count < 63) {
            args[arg_count++] = token;
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL; // NULL terminate the array
        
        printf("Child process: executing '%s' with %d arguments\n", args[0], arg_count);
        
        // Try to execute the command
        execvp(args[0], args);
        
        // If execvp returns, there was an error
        perror("execvp failed");
        exit(1);
    }
    else {
        // Parent process
        int status;
        char buffer[1024];
        ssize_t bytes_read;
        char full_output[OUTPUT_BUFFER_SIZE] = {0};
        
        // Close write end of pipe
        close(pipefd[1]);
        
        printf("Parent process: waiting for child PID %d to finish\n", pid);
        
        // Read output from the pipe
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            strncat(full_output, buffer, OUTPUT_BUFFER_SIZE - strlen(full_output) - 1);
        }
        
        // Wait for child to finish
        wait(&status);
        
        close(pipefd[0]);
        
        if (WIFEXITED(status)) {
            printf("Child process exited with status: %d\n", WEXITSTATUS(status));
            
            // Add the command output to our text buffer
            if (strlen(full_output) > 0) {
                add_text_to_buffer(full_output);
            } else {
                add_text_to_buffer("");  // Just add empty line for commands with no output
            }
        } else {
            printf("Child process did not exit normally\n");
            add_text_to_buffer("Error: Command did not complete normally");
        }
    }
}

// Function to handle Enter key - executes command and shows output
void handle_enter_key(Display *display, Window window, GC gc) {
    printf("ENTER pressed - executing command: '%s'\n", current_command);
    
    // Remove cursor from current position
    text_buffer[cursor_row][cursor_col] = ' ';
    
    // Show the command that was entered (echo)
    if (cursor_row >= BUFFER_ROWS - 1) {
        scroll_buffer();
    } else {
        cursor_row++;
    }
    cursor_col = 0;
    
    // Echo the command
    char echo_message[BUFFER_COLS];
    snprintf(echo_message, sizeof(echo_message), "> %s", current_command);
    int echo_len = strlen(echo_message);
    for (int i = 0; i < echo_len && i < BUFFER_COLS; i++) {
        text_buffer[cursor_row][i] = echo_message[i];
    }
    
    // Execute the command and capture its output
    if (strlen(current_command) > 0) {
        execute_command(display, window, gc, current_command);
    } else {
        // Empty command - just add new prompt
        add_text_to_buffer("");
    }
    
    // Add new prompt
    if (cursor_row >= BUFFER_ROWS - 1) {
        scroll_buffer();
    } else {
        cursor_row++;
        cursor_col = 0;
    }
    
    text_buffer[cursor_row][0] = '>';
    text_buffer[cursor_row][1] = ' ';
    cursor_col = 2;
    
    // Clear command buffer for new input
    memset(current_command, 0, MAX_COMMAND_LENGTH);
    command_length = 0;
    
    // Redraw the window
    draw_text_buffer(display, window, gc);
}

// Function to handle keyboard input
void handle_keypress(Display *display, Window window, GC gc, XKeyEvent *key_event) {
    char buffer[256];
    KeySym keysym;
    int buffer_len;
    int shift_pressed = (key_event->state & ShiftMask);
    
    // Convert keycode to string and keysym
    buffer_len = XLookupString(key_event, buffer, sizeof(buffer) - 1, &keysym, NULL);
    buffer[buffer_len] = '\0';
    
    // Handle special keys
    switch (keysym) {
        case XK_Escape:
            printf("ESC pressed - exiting\n");
            exit(0);
            break;
            
        case XK_Return:
        case XK_KP_Enter:
            handle_enter_key(display, window, gc);
            break;
            
        case XK_BackSpace:
        case XK_Delete:
            // Handle backspace
            if (cursor_col > 2) {  // Don't delete the prompt
                cursor_col--;
                text_buffer[cursor_row][cursor_col] = ' ';
                
                // Update command buffer
                if (command_length > 0) {
                    command_length--;
                    current_command[command_length] = '\0';
                }
            }
            break;
            
        case XK_space:
            // Handle space bar
            if (cursor_col < BUFFER_COLS - 1) {
                text_buffer[cursor_row][cursor_col] = ' ';
                current_command[command_length++] = ' ';
                cursor_col++;
            }
            break;
            
        default:
            // Handle printable characters
            if (buffer_len > 0 && buffer[0] >= 32 && buffer[0] <= 126) {
                if (cursor_col < BUFFER_COLS - 1) {
                    char ch = buffer[0];
                    
                    // Update text buffer
                    text_buffer[cursor_row][cursor_col] = ch;
                    
                    // Update command buffer
                    if (command_length < MAX_COMMAND_LENGTH - 1) {
                        current_command[command_length++] = ch;
                        current_command[command_length] = '\0';
                    }
                    
                    cursor_col++;
                    
                    printf("Current command: '%s'\n", current_command);
                }
            }
            break;
    }
    
    // Redraw the window to show changes (except for Enter which handles its own redraw)
    if (keysym != XK_Return && keysym != XK_KP_Enter) {
        draw_text_buffer(display, window, gc);
    }
}

int main() {
    Display *display;
    Window window;
    XEvent event;
    int screen;
    GC gc;  // Graphics Context
    
    // Initialize the text buffer
    initialize_text_buffer();
    
    // 1. Open connection to the display
    display = XOpenDisplay(NULL);
    if (display == NULL) {
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
        100, 100,        // x, y position
        win_width,       // width
        win_height,      // height
        2,               // border width
        BlackPixel(display, screen),  // border color
        WhitePixel(display, screen)   // background color
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
    XStoreName(display, window, "X11 Shell Terminal with Output Capture");
    
    printf("Shell terminal with output capture created!\n");
    printf("Try commands like: ls, pwd, echo hello, date, whoami\n");
    printf("Press ESC to exit\n");
    
    // 4. Main event loop
    while (1) {
        XNextEvent(display, &event);
        
        switch (event.type) {
            case Expose:
                printf("Expose event - redrawing text buffer\n");
                draw_text_buffer(display, window, gc);
                break;
                
            case KeyPress:
                handle_keypress(display, window, gc, &event.xkey);
                break;
                
            case ButtonPress:
                printf("ButtonPress event received - focusing on window\n");
                // Set focus to window for keyboard input
                XSetInputFocus(display, window, RevertToParent, CurrentTime);
                break;
                
            case ConfigureNotify:
                // Window resized or moved
                break;
        }
    }
    
    // Cleanup (this part won't be reached due to the infinite loop)
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    
    return 0;
}