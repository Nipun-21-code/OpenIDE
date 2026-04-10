//this only works for ubuntu!

#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

/*
================================================================================
                           TERMINAL FILE MANAGER
================================================================================

KEYBINDINGS:
------------

NAVIGATION:
  Up / Down       - Navigate through files and folders
  j / k           - Navigate (vim-style)
  Enter           - Open file in editor / Enter directory
  Backspace       - Go to parent directory
  
FILE OPERATIONS:
  Ctrl+D          - Delete selected file/folder
  Ctrl+R          - Rename selected file/folder
  Ctrl+X          - Move files/folders (multi-select mode)
  
FILE CREATION:
  [+ New File]    - Create a new file (select and press Enter)
  [+ New Folder]  - Create a new folder (select and press Enter)
  
SEARCH:
  /               - Open search interface
  
GENERAL:
  q / Q           - Quit the file manager

MULTI-SELECT MOVE MODE (Ctrl+X):
  Space           - Toggle selection of current file/folder
  Up / Down       - Navigate
  j / k           - Navigate (vim-style)
  Enter           - Confirm selections and proceed to destination selection
  ESC             - Cancel move operation

DESTINATION FOLDER SELECTION:
  Up / Down       - Navigate folders
  j / k           - Navigate (vim-style)
  Enter           - Enter highlighted folder / Go to parent (if ".." selected)
  Backspace       - Go to parent directory
  Ctrl+V          - CONFIRM destination and execute move
  ESC             - Cancel move operation

SEARCH INTERFACE (/):
  Type            - Enter search query
  Backspace       - Delete characters
  Up / Down       - Navigate search results
  Enter           - Open selected result
  ESC             - Close search

EDITOR:
  The file manager will use micro, nano, or vi (in that order of preference)
  for editing files. Install micro with: sudo apt install micro

NOTES:
  - Folders are sorted before files
  - Hidden files (starting with .) are not shown
  - File sizes are displayed in human-readable format (B, KB, MB, GB)
  - Search includes folder names, file names, and file contents (up to 1MB)
  - Maximum search depth: 3 levels
  
================================================================================
*/

#define MAX_ENTRIES 1000
#define MAX_PATH 4096
#define MAX_SEARCH_RESULTS 100
int selected_for_move[MAX_ENTRIES] = {0};
int move_count = 0;

typedef struct {
    char display[512];
    char path[MAX_PATH];
    int type; // 0=folder, 1=filename, 2=content match
    int is_dir;
} SearchResult;

Entry entries[MAX_ENTRIES];
int entry_count = 0;
int selected = 0;
int scroll_offset = 0;
char current_dir[MAX_PATH];

SearchResult search_results[MAX_SEARCH_RESULTS];
int search_result_count = 0;
int search_selected = 0;
int search_scroll = 0;

void format_size(off_t size, char *buf) {
    if (size < 1024) sprintf(buf, "%ldB", size);
    else if (size < 1024*1024) sprintf(buf, "%.1fK", size/1024.0);
    else if (size < 1024*1024*1024) sprintf(buf, "%.1fM", size/(1024.0*1024));
    else sprintf(buf, "%.2fG", size/(1024.0*1024*1024));
}

int compare_entries(const void *a, const void *b) {
    Entry *ea = (Entry*)a, *eb = (Entry*)b;
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;
    return strcmp(ea->name, eb->name);
}

typedef struct {
    char name[256];
    char path[MAX_PATH];
    int is_dir;
    off_t size;
} Entry;

void duplicate_entry() {
    // Check if we have a valid selection
    if (entry_count == 0) return;
    
    Entry *e = &entries[selected];
    
    // Don't allow duplicating special entries
    if (strcmp(e->name, "..") == 0 || 
        strcmp(e->name, "[+ New File]") == 0 ||
        strcmp(e->name, "[+ New Folder]") == 0) {
        return;
    }
    
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win = newwin(9, 60, (height - 9) / 2, (width - 60) / 2);

    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "DUPLICATE");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(win, 3, 2, "Duplicating: %s", e->name);
    mvwprintw(win, 5, 2, "New name: ");
    
    // Pre-fill with original name + "_copy"
    char default_name[256];
    snprintf(default_name, sizeof(default_name), "%s_copy", e->name);
    mvwprintw(win, 5, 12, "%s", default_name);
    wrefresh(win);

    echo();
    curs_set(1);

    char newname[256];
    mvwgetnstr(win, 5, 12, newname, 255);

    noecho();
    curs_set(0);
    delwin(win);

    // If user just pressed enter without typing, use the default name
    if (strlen(newname) == 0) {
        strcpy(newname, default_name);
    }

    if (strlen(newname) > 0 && strcmp(newname, e->name) != 0) {
        char newpath[MAX_PATH];
        snprintf(newpath, MAX_PATH, "%s/%s", current_dir, newname);

        char cmd[MAX_PATH * 2 + 20];
        
        if (e->is_dir) {
            // Copy directory recursively
            snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s' 2>/dev/null", e->path, newpath);
        } else {
            // Copy file
            snprintf(cmd, sizeof(cmd), "cp '%s' '%s' 2>/dev/null", e->path, newpath);
        }
        
        if (system(cmd) == 0) {
            load_directory(current_dir);
            
            // Find and select the newly copied entry
            for (int i = 0; i < entry_count; i++) {
                if (strcmp(entries[i].name, newname) == 0) {
                    selected = i;
                    break;
                }
            }
        }
    }

    clear();
}

// Multi-select mode for moving files
void multi_select_mode() {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // Clear all selections
    memset(selected_for_move, 0, sizeof(selected_for_move));
    move_count = 0;
    
    int selecting = 1;
    int current_pos = selected;
    
    while (selecting) {
        clear();
        
        // Header
        attron(COLOR_PAIR(1) | A_BOLD);
        mvhline(0, 0, ' ', width);
        mvprintw(0, 2, "SELECT FILES TO MOVE (Space:Select | Enter:Done | ESC:Cancel)");
        attroff(COLOR_PAIR(1) | A_BOLD);
        
        // File list with selection indicators
        int list_height = height - 3;
        for (int i = scroll_offset; i < scroll_offset + list_height && i < entry_count; i++) {
            int y = i - scroll_offset + 1;
            Entry *e = &entries[i];
            
            // Skip special entries
            if (strcmp(e->name, "..") == 0 || 
                strcmp(e->name, "[+ New File]") == 0 ||
                strcmp(e->name, "[+ New Folder]") == 0) {
                continue;
            }
            
            if (i == current_pos) {
                attron(COLOR_PAIR(2) | A_REVERSE);
                mvhline(y, 0, ' ', width);
                attroff(COLOR_PAIR(2) | A_REVERSE);
            }
            
            // Selection checkbox
            if (selected_for_move[i]) {
                attron(COLOR_PAIR(3) | A_BOLD);
                mvprintw(y, 2, "[X] ");
                attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                mvprintw(y, 2, "[ ] ");
            }
            
            // File/folder name
            if (e->is_dir) {
                attron(COLOR_PAIR(4));
                mvprintw(y, 6, "[\\] %s", e->name);
                attroff(COLOR_PAIR(4));
            } else {
                mvprintw(y, 6, "[~] %s", e->name);
            }
            
            // Size
            if (!e->is_dir) {
                char size_str[20];
                format_size(e->size, size_str);
                attron(COLOR_PAIR(3));
                mvprintw(y, width - 12, "%10s", size_str);
                attroff(COLOR_PAIR(3));
            } else {
                attron(COLOR_PAIR(4));
                mvprintw(y, width - 12, "    <DIR>");
                attroff(COLOR_PAIR(4));
            }
        }
        
        // Footer with count
        attron(COLOR_PAIR(1));
        mvhline(height-1, 0, ' ', width);
        mvprintw(height-1, 2, "Selected: %d | Space:Toggle | Enter:Continue | ESC:Cancel", move_count);
        attroff(COLOR_PAIR(1));
        
        refresh();
        
        int ch = getch();
        
        switch(ch) {
            case 27: // ESC
                selecting = 0;
                move_count = 0;
                memset(selected_for_move, 0, sizeof(selected_for_move));
                break;
                
            case ' ': // Space to toggle selection
                if (strcmp(entries[current_pos].name, "..") != 0 &&
                    strcmp(entries[current_pos].name, "[+ New File]") != 0 &&
                    strcmp(entries[current_pos].name, "[+ New Folder]") != 0) {
                    
                    if (selected_for_move[current_pos]) {
                        selected_for_move[current_pos] = 0;
                        move_count--;
                    } else {
                        selected_for_move[current_pos] = 1;
                        move_count++;
                    }
                }
                break;
                
            case KEY_UP:
            case 'k':
                if (current_pos > 0) {
                    current_pos--;
                    if (current_pos < scroll_offset) scroll_offset = current_pos;
                }
                break;
                
            case KEY_DOWN:
            case 'j':
                if (current_pos < entry_count - 1) {
                    current_pos++;
                    if (current_pos >= scroll_offset + list_height) {
                        scroll_offset = current_pos - list_height + 1;
                    }
                }
                break;
                
            case 10:
            case 13: // Enter
                if (move_count > 0) {
                    selecting = 0;
                } else {
                    // No files selected, cancel
                    selecting = 0;
                    move_count = 0;
                }
                break;
        }
    }
    
    clear();
}

char* select_destination_folder() {
    static char dest_path[MAX_PATH];
    strcpy(dest_path, current_dir);
    
    int height, width;
    getmaxyx(stdscr, height, width);
    
    int browse_selected = 0;
    int browse_scroll = 0;
    
    // Save original directory to return to later
    char original_dir[MAX_PATH];
    strcpy(original_dir, current_dir);
    
    int selecting = 1;
    
    while (selecting) {
        // Load current directory
        load_directory(dest_path);
        
        clear();
        
        // Header
        attron(COLOR_PAIR(1) | A_BOLD);
        mvhline(0, 0, ' ', width);
        mvprintw(0, 2, "SELECT DESTINATION FOLDER");
        attroff(COLOR_PAIR(1) | A_BOLD);
        
        // Current path
        attron(COLOR_PAIR(3));
        mvprintw(1, 2, "Destination: %s", dest_path);
        attroff(COLOR_PAIR(3));
        
        // File list (only show folders)
        int list_height = height - 4;
        int display_idx = 0;
        
        for (int i = 0; i < entry_count && display_idx < list_height; i++) {
            Entry *e = &entries[i];
            
            // Only show folders and parent dir
            if (!e->is_dir && strcmp(e->name, "..") != 0) continue;
            
            int y = display_idx + 2;
            
            if (display_idx == browse_selected) {
                attron(COLOR_PAIR(2) | A_REVERSE);
                mvhline(y, 0, ' ', width);
                attroff(COLOR_PAIR(2) | A_REVERSE);
            }
            
            if (strcmp(e->name, "..") == 0) {
                mvprintw(y, 2, "+- [\\] ..");
            } else {
                attron(COLOR_PAIR(4));
                mvprintw(y, 2, "|- [\\] %s", e->name);
                attroff(COLOR_PAIR(4));
            }
            
            display_idx++;
        }
        
        // Footer - CORRECT controls
        attron(COLOR_PAIR(1));
        mvhline(height-1, 0, ' ', width);
        mvprintw(height-1, 2, "^V:Confirm Move Here | Enter:Open Folder | Backspace:Parent | ESC:Cancel");
        attroff(COLOR_PAIR(1));
        
        refresh();
        
        int ch = getch();
        
        switch(ch) {
            case 27: // ESC
                selecting = 0;
                dest_path[0] = '\0'; // Empty string means cancel
                break;
                
            case 22: // Ctrl+V - CONFIRM DESTINATION
                selecting = 0;
                break;
                
            case KEY_UP:
            case 'k':
                if (browse_selected > 0) {
                    browse_selected--;
                }
                break;
                
            case KEY_DOWN:
            case 'j':
                // Count visible folders
                int folder_count = 0;
                for (int i = 0; i < entry_count; i++) {
                    if (entries[i].is_dir || strcmp(entries[i].name, "..") == 0) {
                        folder_count++;
                    }
                }
                if (browse_selected < folder_count - 1) {
                    browse_selected++;
                }
                break;
                
            case 10:
            case 13: // Enter - OPEN THE SELECTED FOLDER
                {
                    int folder_idx = 0;
                    for (int i = 0; i < entry_count; i++) {
                        if (entries[i].is_dir || strcmp(entries[i].name, "..") == 0) {
                            if (folder_idx == browse_selected) {
                                if (strcmp(entries[i].name, "..") == 0) {
                                    // Go to parent
                                    char *last_slash = strrchr(dest_path, '/');
                                    if (last_slash && last_slash != dest_path) {
                                        *last_slash = '\0';
                                    } else if (strcmp(dest_path, "/") != 0) {
                                        strcpy(dest_path, "/");
                                    }
                                } else {
                                    // Enter the selected folder
                                    strcpy(dest_path, entries[i].path);
                                }
                                browse_selected = 0;
                                break;
                            }
                            folder_idx++;
                        }
                    }
                }
                break;
                
            case KEY_BACKSPACE:
            case 127:
            case 8: // Backspace - Go to parent
                if (strcmp(dest_path, "/") != 0) {
                    char *last_slash = strrchr(dest_path, '/');
                    if (last_slash && last_slash != dest_path) {
                        *last_slash = '\0';
                    } else {
                        strcpy(dest_path, "/");
                    }
                    browse_selected = 0;
                }
                break;
        }
    }
    
    // Restore original directory
    strcpy(current_dir, original_dir);
    load_directory(current_dir);
    
    if (strlen(dest_path) == 0) {
        return NULL;
    }
    
    return dest_path;
}

// Execute the move operation
void execute_move(const char *dest_folder) {
    int moved = 0;
    
    for (int i = 0; i < entry_count; i++) {
        if (selected_for_move[i]) {
            Entry *e = &entries[i];
            char dest_path[MAX_PATH];
            snprintf(dest_path, MAX_PATH, "%s/%s", dest_folder, e->name);
            
            char cmd[MAX_PATH * 2 + 20];
            snprintf(cmd, sizeof(cmd), "mv '%s' '%s' 2>/dev/null", e->path, dest_path);
            
            if (system(cmd) == 0) {
                moved++;
            }
        }
    }
    
    // Clear selections
    memset(selected_for_move, 0, sizeof(selected_for_move));
    move_count = 0;
    
    // Reload directory
    load_directory(current_dir);
    
    // Show result
    int height, width;
    getmaxyx(stdscr, height, width);
    
    WINDOW *win = newwin(7, 50, (height - 7) / 2, (width - 50) / 2);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "MOVE COMPLETE");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);
    
    mvwprintw(win, 3, 2, "Moved %d file(s)/folder(s)", moved);
    mvwprintw(win, 5, 2, "Press any key to continue");
    
    wrefresh(win);
    wgetch(win);
    delwin(win);
    
    clear();
}

// Main move workflow - call this from the main loop when Ctrl+X is pressed
void move_files_workflow() {
    // Step 1: Multi-select files
    multi_select_mode();
    
    if (move_count == 0) {
        clear();
        return;
    }
    
    // Step 2: Select destination
    char *dest = select_destination_folder();
    
    if (dest == NULL) {
        // User cancelled
        memset(selected_for_move, 0, sizeof(selected_for_move));
        move_count = 0;
        clear();
        return;
    }
    
    // Step 3: Execute move
    execute_move(dest);
}

int check_and_setup_editor() {
    // Check if nano is available
    if (system("which nano >/dev/null 2>&1") == 0) {
        return 0; // nano is installed
    }
    
    // Check if micro is available
    if (system("which micro >/dev/null 2>&1") == 0) {
        return 0; // micro is installed
    }
    
    // Neither found, install micro
    int height, width;
    getmaxyx(stdscr, height, width);
    
    WINDOW *win = newwin(9, 60, (height - 9) / 2, (width - 60) / 2);
    
    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "INSTALLING EDITOR");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);
    
    mvwprintw(win, 3, 2, "No editor found (nano/micro)");
    mvwprintw(win, 4, 2, "Installing micro editor...");
    mvwprintw(win, 6, 2, "Press any key to continue");
    wrefresh(win);
    wgetch(win);
    
    delwin(win);
    
    // Temporarily exit ncurses mode to show installation output
    endwin();
    
    printf("Installing micro editor...\n");
    int result = system("sudo apt install micro -y");
    
    printf("\nPress Enter to continue...");
    getchar();
    
    // Reinitialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    clear();
    
    return result;
}

void load_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;

    entry_count = 0;
    struct dirent *ent;

    // Add parent directory
    if (strcmp(path, "/") != 0) {
        Entry *e = &entries[entry_count];
        strcpy(e->name, "..");
        snprintf(e->path, MAX_PATH, "%s/..", path);
        e->is_dir = 1;
        entry_count++;
    }

    // Add "New File" option
    Entry *new_e = &entries[entry_count];
    strcpy(new_e->name, "[+ New File]");
    strcpy(new_e->path, "");
    new_e->is_dir = 0;
    new_e->size = 0;
    entry_count++;
    
    // Add "New Folder" option
    Entry *new_f = &entries[entry_count];
    strcpy(new_f->name, "[+ New Folder]");
    strcpy(new_f->path, "");
    new_f->is_dir = 0;
    new_f->size = 0;
    entry_count++;

    while ((ent = readdir(dir)) && entry_count < MAX_ENTRIES) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (ent->d_name[0] == '.') continue;

        Entry *e = &entries[entry_count];
        strncpy(e->name, ent->d_name, 255);
        snprintf(e->path, MAX_PATH, "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(e->path, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size = st.st_size;
            entry_count++;
        }
    }
    closedir(dir);

    if (entry_count > 3) {  // Changed from 2 to 3
        int start = (strcmp(entries[0].name, "..") == 0) ? 3 : 2;  // Changed from 2:1 to 3:2
        qsort(entries + start, entry_count - start, sizeof(Entry), compare_entries);
    }

    selected = 0;
    scroll_offset = 0;
}

void create_new_folder() {
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win = newwin(7, 60, (height - 7) / 2, (width - 60) / 2);

    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "CREATE NEW FOLDER");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(win, 3, 2, "Enter folder name: ");
    wrefresh(win);

    echo();
    curs_set(1);

    char foldername[256];
    mvwgetnstr(win, 3, 21, foldername, 255);
    
    // Add this check here:
    if (strlen(foldername) >= 255) {
        foldername[254] = '\0';
    }
    
    noecho();
    curs_set(0);
    delwin(win);

    if (strlen(foldername) > 0) {
        char folderpath[MAX_PATH];
        snprintf(folderpath, MAX_PATH, "%s/%s", current_dir, foldername);

        // Create directory with default permissions (0755)
        if (mkdir(folderpath, 0755) == 0) {
            load_directory(current_dir);

            // Find and select the newly created folder
            for (int i = 0; i < entry_count; i++) {
                if (strcmp(entries[i].name, foldername) == 0) {
                    selected = i;
                    break;
                }
            }
        }
    }

    clear();
}

void move_entry(Entry *e) {
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win = newwin(11, 70, (height - 11) / 2, (width - 70) / 2);

    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "MOVE");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(win, 3, 2, "Moving: %s", e->name);
    mvwprintw(win, 4, 2, "From:   %s", current_dir);
    mvwprintw(win, 6, 2, "Enter destination path:");
    mvwprintw(win, 7, 2, "(relative or absolute)");
    mvwprintw(win, 8, 2, "To: ");
    wrefresh(win);

    echo();
    curs_set(1);

    char dest_path[MAX_PATH];
    mvwgetnstr(win, 8, 6, dest_path, MAX_PATH - 1);

    noecho();
    curs_set(0);
    delwin(win);

    if (strlen(dest_path) > 0) {
        char resolved_dest[MAX_PATH];
        char final_dest[MAX_PATH];
        
        // Resolve destination path
        if (dest_path[0] == '/') {
            // Absolute path
            strncpy(resolved_dest, dest_path, MAX_PATH);
        } else {
            // Relative path - resolve from current directory
            snprintf(resolved_dest, MAX_PATH, "%s/%s", current_dir, dest_path);
        }
        
        // Check if destination is a directory
        struct stat st;
        if (stat(resolved_dest, &st) == 0 && S_ISDIR(st.st_mode)) {
            // Destination is a directory, move file into it
            snprintf(final_dest, MAX_PATH, "%s/%s", resolved_dest, e->name);
        } else {
            // Use as-is (for renaming during move)
            strncpy(final_dest, resolved_dest, MAX_PATH);
        }

        char cmd[MAX_PATH * 2 + 20];
        snprintf(cmd, sizeof(cmd), "mv '%s' '%s' 2>/dev/null", e->path, final_dest);
        
        if (system(cmd) == 0) {
            load_directory(current_dir);
        }
    }

    clear();
}

void rename_entry(Entry *e) {
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win = newwin(9, 60, (height - 9) / 2, (width - 60) / 2);

    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "RENAME");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(win, 3, 2, "Current: %s", e->name);
    mvwprintw(win, 5, 2, "New name: ");
    wrefresh(win);

    echo();
    curs_set(1);

    char newname[256];
    mvwgetnstr(win, 5, 12, newname, 255);

    noecho();
    curs_set(0);
    delwin(win);

    if (strlen(newname) > 0 && strcmp(newname, e->name) != 0) {
        char newpath[MAX_PATH];
        snprintf(newpath, MAX_PATH, "%s/%s", current_dir, newname);

        char cmd[MAX_PATH * 2 + 20];
        snprintf(cmd, sizeof(cmd), "mv '%s' '%s' 2>/dev/null", e->path, newpath);
        
        if (system(cmd) == 0) {
            load_directory(current_dir);
            
            // Find and select the renamed entry
            for (int i = 0; i < entry_count; i++) {
                if (strcmp(entries[i].name, newname) == 0) {
                    selected = i;
                    break;
                }
            }
        }
    }

    clear();
}

void draw_ui() {
    int height, width;
    getmaxyx(stdscr, height, width);
    clear();

    // Header
    attron(COLOR_PAIR(1) | A_BOLD);
    mvhline(0, 0, ' ', width);
    char *dir_name = strrchr(current_dir, '/');
    dir_name = dir_name ? dir_name + 1 : current_dir;
    if (strlen(dir_name) == 0) dir_name = "/";
    mvprintw(0, 2, "[\\] %s", dir_name);
    attroff(COLOR_PAIR(1) | A_BOLD);

    // File list
    int list_height = height - 3;
    for (int i = scroll_offset; i < scroll_offset + list_height && i < entry_count; i++) {
        int y = i - scroll_offset + 1;
        Entry *e = &entries[i];

        if (i == selected) {
            attron(COLOR_PAIR(2) | A_REVERSE);
            mvhline(y, 0, ' ', width);
            attroff(COLOR_PAIR(2) | A_REVERSE);
        }

        // Draw tree line
        if (strcmp(e->name, "..") == 0) {
            mvprintw(y, 2, "+- [\\] %s", e->name);
        } else if (strcmp(e->name, "[+ New File]") == 0) {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(y, 2, "+- %s", e->name);
            attroff(COLOR_PAIR(3) | A_BOLD);
        } else if (strcmp(e->name, "[+ New Folder]") == 0) {
            attron(COLOR_PAIR(4) | A_BOLD);  // Using green (COLOR_PAIR(4))
            mvprintw(y, 2, "+- %s", e->name);
            attroff(COLOR_PAIR(4) | A_BOLD);
        } else {
            int is_last = (i == entry_count - 1);
            if (is_last) {
                mvprintw(y, 2, "`- %s %s", e->is_dir ? "[\\]" : "[~]", e->name);
            } else {
                mvprintw(y, 2, "|- %s %s", e->is_dir ? "[\\]" : "[~]", e->name);
            }
        }

        // Size/type indicator
        if (!e->is_dir && strcmp(e->name, "[+ New File]") != 0) {
            char size_str[20];
            format_size(e->size, size_str);
            attron(COLOR_PAIR(3));
            mvprintw(y, width - 12, "%10s", size_str);
            attroff(COLOR_PAIR(3));
        } else if (e->is_dir) {
            attron(COLOR_PAIR(4));
            mvprintw(y, width - 12, "    <DIR>");
            attroff(COLOR_PAIR(4));
        }
    }

    // Footer
    attron(COLOR_PAIR(1));
    mvhline(height-1, 0, ' ', width);
    mvprintw(height-1, 2, "Enter:Open | ^D:Del | ^R:Rename | ^X:Move | /:Search | Back:.. | q:Quit | ^E:Dup");
    attroff(COLOR_PAIR(1));

    refresh();
}

void navigate_to(const char *path) {
    char resolved[MAX_PATH];
    if (realpath(path, resolved)) {
        strcpy(current_dir, resolved);
        load_directory(current_dir);
    }
}

void open_file(const char *path) {
    endwin();
    char cmd[MAX_PATH + 20];

    if (system("which micro > /dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), "micro '%s'", path);
    } else if (system("which nano > /dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), "nano '%s'", path);
    } else {
        snprintf(cmd, sizeof(cmd), "vi '%s'", path);
    }

    system(cmd);

    // Reinitialize ncurses after editor closes
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    refresh();
}

void create_new_file() {
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win = newwin(7, 60, (height - 7) / 2, (width - 60) / 2);

    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "CREATE NEW FILE");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(win, 3, 2, "Enter filename: ");
    wrefresh(win);

    echo();
    curs_set(1);

    char filename[256];
    mvwgetnstr(win, 3, 18, filename, 255);
    
    // Add this check here:
    if (strlen(filename) >= 255) {
        filename[254] = '\0';
    }
    
    noecho();
    curs_set(0);
    delwin(win);

    if (strlen(filename) > 0) {
        char filepath[MAX_PATH];
        snprintf(filepath, MAX_PATH, "%s/%s", current_dir, filename);

        FILE *f = fopen(filepath, "w");
        if (f) {
            fclose(f);
            load_directory(current_dir);

            for (int i = 0; i < entry_count; i++) {
                if (strcmp(entries[i].name, filename) == 0) {
                    selected = i;
                    break;
                }
            }
        }
    }

    clear();
}

void delete_entry(Entry *e) {
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win = newwin(7, 60, (height - 7) / 2, (width - 60) / 2);

    box(win, 0, 0);
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "DELETE CONFIRMATION");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(win, 3, 2, "Delete: %s", e->name);
    if (e->is_dir) {
        wattron(win, A_BOLD);
        mvwprintw(win, 4, 2, "WARNING: Entire directory will be deleted!");
        wattroff(win, A_BOLD);
    }
    mvwprintw(win, 5, 2, "Press 'y' to confirm, any other key to cancel");

    wrefresh(win);

    int ch = wgetch(win);
    delwin(win);

    if (ch == 'y' || ch == 'Y') {
        char cmd[MAX_PATH + 30];
        if (e->is_dir) {
            snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", e->path);
        } else {
            snprintf(cmd, sizeof(cmd), "rm '%s' 2>/dev/null", e->path);
        }
        system(cmd);

        load_directory(current_dir);
        if (selected >= entry_count) selected = entry_count - 1;
        if (selected < 0) selected = 0;
    }

    clear();
}

int case_insensitive_strstr(const char *haystack, const char *needle) {
    if (!*needle) return 1;

    int needle_len = strlen(needle);
    int haystack_len = strlen(haystack);

    for (int i = 0; i <= haystack_len - needle_len; i++) {
        int match = 1;
        for (int j = 0; j < needle_len; j++) {
            if (tolower(haystack[i + j]) != tolower(needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

void search_in_file(const char *filepath, const char *query, const char *display_path) {
    if (search_result_count >= MAX_SEARCH_RESULTS) return;

    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char line[1024];
    int line_num = 1;
    int found = 0;

    while (fgets(line, sizeof(line), f) && !found) {
        if (case_insensitive_strstr(line, query)) {
            SearchResult *r = &search_results[search_result_count++];
            r->type = 2; // content match
            r->is_dir = 0;

            // Trim line
            int len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

            // Truncate if too long
            if (strlen(line) > 60) {
                line[57] = '.';
                line[58] = '.';
                line[59] = '.';
                line[60] = '\0';
            }

            snprintf(r->display, sizeof(r->display), "[~] %s:%d: %s", display_path, line_num, line);
            strncpy(r->path, filepath, MAX_PATH);
            found = 1;
        }
        line_num++;
    }

    fclose(f);
}

void recursive_search(const char *base_path, const char *query, int max_depth, int current_depth) {
    if (current_depth > max_depth || search_result_count >= MAX_SEARCH_RESULTS) return;

    DIR *dir = opendir(base_path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) && search_result_count < MAX_SEARCH_RESULTS) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (ent->d_name[0] == '.') continue;

        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s/%s", base_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);

        // Check if name matches
        if (case_insensitive_strstr(ent->d_name, query)) {
            SearchResult *r = &search_results[search_result_count++];
            r->type = is_dir ? 0 : 1;
            r->is_dir = is_dir;

            // Make path relative to current_dir
            char *rel_path = full_path + strlen(current_dir);
            if (*rel_path == '/') rel_path++;

            if (is_dir) {
                snprintf(r->display, sizeof(r->display), "[\\] %s", rel_path);
            } else {
                snprintf(r->display, sizeof(r->display), "[~] %s", rel_path);
            }
            strncpy(r->path, full_path, MAX_PATH);
        }

        // Recurse into directories
        if (is_dir) {
            recursive_search(full_path, query, max_depth, current_depth + 1);
        }
        // Search file contents for non-directories
        else if (!is_dir && st.st_size < 1024 * 1024) { // Only search files < 1MB
            char *rel_path = full_path + strlen(current_dir);
            if (*rel_path == '/') rel_path++;
            search_in_file(full_path, query, rel_path);
        }
    }

    closedir(dir);
}

int compare_search_results(const void *a, const void *b) {
    SearchResult *ra = (SearchResult*)a;
    SearchResult *rb = (SearchResult*)b;

    if (ra->type != rb->type) return ra->type - rb->type;
    return strcmp(ra->display, rb->display);
}

void perform_search(const char *query) {
    search_result_count = 0;
    search_selected = 0;
    search_scroll = 0;

    if (strlen(query) == 0) return;

    recursive_search(current_dir, query, 3, 0); // max depth 3

    // Sort: folders first, then files, then content
    qsort(search_results, search_result_count, sizeof(SearchResult), compare_search_results);
}

void show_search_ui() {
    int height, width;
    getmaxyx(stdscr, height, width);

    int win_height = height - 6;
    int win_width = width - 10;
    if (win_width > 100) win_width = 100;

    WINDOW *win = newwin(win_height, win_width, 3, (width - win_width) / 2);

    char query[256] = "";
    int query_len = 0;

    int running = 1;

    while (running) {
        // Perform search
        perform_search(query);

        // Draw window
        werase(win);
        box(win, 0, 0);

        wattron(win, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(win, 0, 2, " SEARCH ");
        wattroff(win, COLOR_PAIR(1) | A_BOLD);

        // Search input
        wattron(win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(win, 1, 2, "> %s", query);
        wattroff(win, COLOR_PAIR(3) | A_BOLD);

        // Separator
        mvwhline(win, 2, 1, ACS_HLINE, win_width - 2);

        // Results
        int result_height = win_height - 5;

        if (search_result_count == 0 && strlen(query) > 0) {
            wattron(win, COLOR_PAIR(3));
            mvwprintw(win, 4, 2, "No results found");
            wattroff(win, COLOR_PAIR(3));
        } else if (strlen(query) == 0) {
            wattron(win, COLOR_PAIR(3));
            mvwprintw(win, 4, 2, "Type to search...");
            wattroff(win, COLOR_PAIR(3));
        } else {
            for (int i = search_scroll; i < search_scroll + result_height && i < search_result_count; i++) {
                int y = i - search_scroll + 3;
                SearchResult *r = &search_results[i];

                if (i == search_selected) {
                    wattron(win, A_REVERSE);
                }

                // Color based on type
                if (r->type == 0) { // folder
                    wattron(win, COLOR_PAIR(4));
                } else if (r->type == 1) { // filename
                    wattron(win, COLOR_PAIR(3));
                }

                mvwprintw(win, y, 2, "%-*.*s", win_width - 4, win_width - 4, r->display);

                if (r->type == 0) {
                    wattroff(win, COLOR_PAIR(4));
                } else if (r->type == 1) {
                    wattroff(win, COLOR_PAIR(3));
                }

                if (i == search_selected) {
                    wattroff(win, A_REVERSE);
                }
            }
        }

        // Footer
        wattron(win, COLOR_PAIR(1));
        mvwhline(win, win_height - 2, 1, ' ', win_width - 2);
        mvwprintw(win, win_height - 2, 2, "Enter:Open | ESC:Close | Results:%d", search_result_count);
        wattroff(win, COLOR_PAIR(1));

        wrefresh(win);

        int ch = wgetch(win);

        switch(ch) {
            case 27: // ESC
                running = 0;
                break;

            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (query_len > 0) {
                    query[--query_len] = '\0';
                    search_selected = 0;
                    search_scroll = 0;
                }
                break;

            case KEY_UP:
                if (search_selected > 0) {
                    search_selected--;
                    if (search_selected < search_scroll) {
                        search_scroll = search_selected;
                    }
                }
                break;

            case KEY_DOWN:
                if (search_selected < search_result_count - 1) {
                    search_selected++;
                    if (search_selected >= search_scroll + result_height) {
                        search_scroll = search_selected - result_height + 1;
                    }
                }
                break;

            case 10:
            case 13: // Enter
                if (search_result_count > 0) {
                    SearchResult *r = &search_results[search_selected];
                    running = 0;
                    delwin(win);
                    clear();
                    refresh();

                    if (r->is_dir) {
                        navigate_to(r->path);
                    } else {
                        open_file(r->path);
                        load_directory(current_dir); // Reload in case file was modified
                    }
                    return;
                }
                break;

            case  18: // Ctrl+R for rename
                if (entry_count > 0 && strcmp(entries[selected].name, "..") != 0
                    && strcmp(entries[selected].name, "[+ New File]") != 0
                    && strcmp(entries[selected].name, "[+ New Folder]") != 0) {
                    rename_entry(&entries[selected]);
                }
                break;
            
            default:
                if (ch >= 32 && ch < 127 && query_len < 255) {
                    query[query_len++] = ch;
                    query[query_len] = '\0';
                    search_selected = 0;
                    search_scroll = 0;
                }
                break;
        }
    }

    delwin(win);
    clear();
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        strcpy(current_dir, argv[1]);
    } else {
        getcwd(current_dir, MAX_PATH);
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLUE);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_CYAN, COLOR_BLACK);
    init_pair(4, COLOR_GREEN, COLOR_BLACK);

    check_and_setup_editor();

    load_directory(current_dir);

    int running = 1;
    while (running) {
        draw_ui();
        int ch = getch();
        int height = getmaxy(stdscr) - 3;

        switch(ch) {
            case 'q':
            case 'Q':
                running = 0;
                break;

            case '/':
                show_search_ui();
                break;

            case KEY_UP:
            case 'k':
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) scroll_offset = selected;
                }
                break;

            case KEY_DOWN:
            case 'j':
                if (selected < entry_count - 1) {
                    selected++;
                    if (selected >= scroll_offset + height) scroll_offset = selected - height + 1;
                }
                break;

            case 10:
            case 13:
                if (entry_count > 0) {
                    if (strcmp(entries[selected].name, "[+ New File]") == 0) {
                        create_new_file();
                    } else if (strcmp(entries[selected].name, "[+ New Folder]") == 0) {
                        create_new_folder();  // Add this new condition
                    } else if (entries[selected].is_dir) {
                        navigate_to(entries[selected].path);
                    } else {
                        open_file(entries[selected].path);
                    }
                }
                break;

            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (strcmp(current_dir, "/") != 0) {
                    navigate_to("..");
                }
                break;

            case 4: // Ctrl+D for delete
                if (entry_count > 0 && strcmp(entries[selected].name, "..") != 0
                    && strcmp(entries[selected].name, "[+ New File]") != 0
                    && strcmp(entries[selected].name, "[+ New Folder]") != 0) {
                    delete_entry(&entries[selected]);
                }
                break;
            
            case 18: // Ctrl+R for rename (18 is the ASCII code for Ctrl+R)
                    if (entry_count > 0 && strcmp(entries[selected].name, "..") != 0
                        && strcmp(entries[selected].name, "[+ New File]") != 0
                        && strcmp(entries[selected].name, "[+ New Folder]") != 0) {
                        rename_entry(&entries[selected]);
                    }
                    break;
                
             case 24: // Ctrl+X for move
                    move_files_workflow();
                    break;
             case 5: // Ctrl+E for duplicate
                     duplicate_entry();
                     break;
        }
    }

    endwin();
    return 0;
}
