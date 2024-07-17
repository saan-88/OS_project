#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

// Define a tree node structure to represent a process
struct process_node {
    pid_t pid;                        // Process ID
    struct process_node *first_child; // Pointer to the first child process
    struct process_node *next_sibling;// Pointer to the next sibling process
};

// Function to check if a directory name is numeric (i.e., it represents a process ID)
int is_numeric_directory(const char *name) {
    for (int i = 0; name[i] != '\0'; i++) {
        if (!isdigit(name[i])) {
            return 0; // Return 0 (false) if any character is not a digit
        }
    }
    return 1; // Return 1 (true) if all characters are digits
}

// Function to get the parent PID (PPID) of a given process
pid_t get_ppid(pid_t pid) {
    char path[256], line[256];
    FILE *status_file;
    pid_t ppid = 0;

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    status_file = fopen(path, "r");
    if (status_file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), status_file) != NULL) {
        if (strncmp(line, "PPid:", 5) == 0) {
            sscanf(line, "PPid: %d", &ppid);
            break;
        }
    }
    fclose(status_file);
    return ppid;
}

// Function to create a new process node
struct process_node* create_node(pid_t pid) {
    struct process_node* new_node = (struct process_node*)malloc(sizeof(struct process_node));
    new_node->pid = pid;
    new_node->first_child = NULL;
    new_node->next_sibling = NULL;
    return new_node;
}

// Function to add a child to a parent node
void add_child(struct process_node* parent, struct process_node* child) {
    if (parent->first_child == NULL) {
        parent->first_child = child;
    } else {
        struct process_node* sibling = parent->first_child;
        while (sibling->next_sibling != NULL) {
            sibling = sibling->next_sibling;
        }
        sibling->next_sibling = child;
    }
}

// Function to build the process tree
struct process_node* build_process_tree(pid_t root_pid) {
    DIR *proc_dir;
    struct dirent *entry;
    struct process_node *root = create_node(root_pid);

    proc_dir = opendir("/proc");
    if (proc_dir == NULL) {
        perror("Unable to open /proc");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(proc_dir)) != NULL) {
        if (entry->d_type == DT_DIR && is_numeric_directory(entry->d_name)) {
            pid_t pid = (pid_t)atoi(entry->d_name);
            pid_t ppid = get_ppid(pid);

            if (ppid == root_pid) {
                struct process_node* child_node = build_process_tree(pid);
                add_child(root, child_node);
            }
        }
    }
    closedir(proc_dir);
    return root;
}

// Function to kill a process tree
void kill_process_tree(struct process_node* node) {
    if (node == NULL) {
        return;
    }

    struct process_node* child = node->first_child;
    while (child != NULL) {
        kill_process_tree(child);
        child = child->next_sibling;
    }

    // Kill the current process
    if (kill(node->pid, SIGKILL) == -1) {
        if (errno != ESRCH) {
            perror("Failed to kill process");
        }
    } else {
        printf("Killed process %d\n", node->pid);
    }

    free(node); // Free the memory allocated for the node
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <PID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    pid_t root_pid = (pid_t)atoi(argv[1]);
    if (root_pid <= 1) {
        fprintf(stderr, "Invalid PID %d\n", root_pid);
        exit(EXIT_FAILURE);
    }

    struct process_node* root = build_process_tree(root_pid);
    kill_process_tree(root);

    return 0;
}
