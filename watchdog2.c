#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

GtkWidget *process_list_view;
GtkWidget *email_entry;
GtkWidget *pid_entry;
GtkWidget *start_button;
GtkWidget *stop_button;

int watchdog_running = 0;
GList *monitored_processes = NULL;

typedef struct {
    int pid;
    char command[256];
    char status[20];
    time_t last_restart;
} Process;

static void get_process_command(int pid, char *command, size_t size) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (f) {
        size_t read = fread(command, 1, size - 1, f);
        command[read] = '\0';
        fclose(f);
    } else {
        snprintf(command, size, "Unknown");
    }
}

static void update_process_list() {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(process_list_view)));
    gtk_list_store_clear(store);

    GList *iterator = monitored_processes;
    while (iterator != NULL) {
        Process *proc = (Process *)iterator->data;
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 
                          0, proc->pid,
                          1, proc->command,
                          2, proc->status,
                          -1);
        iterator = iterator->next;
    }
}

static void send_email(const char *email, const char *message) {

    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0';

    char email_content[512];
    snprintf(email_content, sizeof(email_content),
             "Process Watchdog Alert - %s\n\n%s",
             timestamp, message);

    FILE *mail_pipe = popen("/usr/sbin/sendmail -t", "w");
    if (mail_pipe != NULL) {
        fprintf(mail_pipe, "To: %s\n", email);
        fprintf(mail_pipe, "From: process-watchdog@localhost\n");
        fprintf(mail_pipe, "Subject: Process Watchdog Alert\n\n");
        fprintf(mail_pipe, "%s\n", email_content);
        pclose(mail_pipe);
    }
}

static void restart_process(Process *proc) {
    if (proc->command[0] != '\0') {
        pid_t new_pid = fork();
        if (new_pid == 0) {
            setsid();
            //system(proc->command);

	    execl("/bin/sh", "sh", "-c", proc->command, NULL);
            //char *argv[] = {"/bin/sh", "-c", proc->command, NULL};
            //execv("/bin/sh", argv);
	    exit(0);
        } else if (new_pid > 0) {
            proc->pid = new_pid;
            proc->last_restart = time(NULL);
            strcpy(proc->status, "Running");
        }
    }
}

static void add_process(GtkWidget *widget, gpointer data) {
    const char *pid_str = gtk_entry_get_text(GTK_ENTRY(pid_entry));
    int pid = atoi(pid_str);
    
    if (pid <= 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK,
                                                 "Invalid PID entered");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    if (kill(pid, 0) == -1) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK,
                                                 "Process with PID %d does not exist",
                                                 pid);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    GList *iterator = monitored_processes;
    while (iterator != NULL) {
        Process *proc = (Process *)iterator->data;
        if (proc->pid == pid) {
            GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                                     GTK_DIALOG_MODAL,
                                                     GTK_MESSAGE_ERROR,
                                                     GTK_BUTTONS_OK,
                                                     "Process is already being monitored");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
        iterator = iterator->next;
    }

    Process *new_process = malloc(sizeof(Process));
    new_process->pid = pid;
    new_process->last_restart = time(NULL);
    strcpy(new_process->status, "Running");
    get_process_command(pid, new_process->command, sizeof(new_process->command));
    
    monitored_processes = g_list_append(monitored_processes, new_process);
    update_process_list();
    
    gtk_entry_set_text(GTK_ENTRY(pid_entry), "");
}

static void remove_process(GtkWidget *widget, gpointer data) {
    const char *pid_str = gtk_entry_get_text(GTK_ENTRY(pid_entry));
    int pid = atoi(pid_str);
    
    GList *iterator = monitored_processes;
    while (iterator != NULL) {
        Process *proc = (Process *)iterator->data;
        if (proc->pid == pid) {
            monitored_processes = g_list_remove(monitored_processes, proc);
            free(proc);
            break;
        }
        iterator = iterator->next;
    }
    update_process_list();
    
    gtk_entry_set_text(GTK_ENTRY(pid_entry), "");
}

static gboolean monitor_processes(gpointer user_data) {
    const char *email = gtk_entry_get_text(GTK_ENTRY(email_entry));
    GList *iterator = monitored_processes;
    
    while (iterator != NULL) {
        Process *proc = (Process *)iterator->data;
        if (kill(proc->pid, 0) == -1) {
            char message[512];
            snprintf(message, sizeof(message),
                    "Process %d (%s) has stopped. Attempting to restart...",
                    proc->pid, proc->command);
            
            strcpy(proc->status, "Restarting");
            update_process_list();
            
            send_email(email, message);
            
            restart_process(proc);
            
            update_process_list();
        }
        iterator = iterator->next;
    }
    
    return watchdog_running;
}

static void start_watchdog(GtkWidget *widget, gpointer data) {
    if (watchdog_running) return;
    
    const char *email = gtk_entry_get_text(GTK_ENTRY(email_entry));
    if (strlen(email) == 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK,
                                                 "Please enter an email address");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    watchdog_running = 1;
    g_timeout_add_seconds(5, monitor_processes, NULL);
    gtk_widget_set_sensitive(start_button, FALSE);
    gtk_widget_set_sensitive(stop_button, TRUE);
}

static void stop_watchdog(GtkWidget *widget, gpointer data) {
    if (!watchdog_running) return;
    watchdog_running = 0;
    gtk_widget_set_sensitive(start_button, TRUE);
    gtk_widget_set_sensitive(stop_button, FALSE);
}

static GtkWidget *setup_process_list_view() {
    GtkListStore *store = gtk_list_store_new(3,
                                           G_TYPE_INT,     
                                           G_TYPE_STRING,  
                                           G_TYPE_STRING); 
    
    GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    
    GtkTreeViewColumn *pid_column = gtk_tree_view_column_new_with_attributes(
        "PID", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), pid_column);
    
    GtkTreeViewColumn *cmd_column = gtk_tree_view_column_new_with_attributes(
        "Command", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), cmd_column);
    
    GtkTreeViewColumn *status_column = gtk_tree_view_column_new_with_attributes(
        "Status", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), status_column);
    
    g_object_unref(store);
    return tree_view;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Process Watchdog");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    GtkWidget *email_label = gtk_label_new("Email Address:");
    gtk_box_pack_start(GTK_BOX(vbox), email_label, FALSE, FALSE, 2);
    
    email_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Enter your email address");
    gtk_box_pack_start(GTK_BOX(vbox), email_entry, FALSE, FALSE, 2);

    GtkWidget *pid_label = gtk_label_new("Process PID:");
    gtk_box_pack_start(GTK_BOX(vbox), pid_label, FALSE, FALSE, 2);
    
    pid_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pid_entry), "Enter process PID");
    gtk_box_pack_start(GTK_BOX(vbox), pid_entry, FALSE, FALSE, 2);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);
    
    process_list_view = setup_process_list_view();
    gtk_container_add(GTK_CONTAINER(scrolled_window), process_list_view);

    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    GtkWidget *add_button = gtk_button_new_with_label("Add Process");
    GtkWidget *remove_button = gtk_button_new_with_label("Remove Process");
    start_button = gtk_button_new_with_label("Start Watchdog");
    stop_button = gtk_button_new_with_label("Stop Watchdog");
    
    gtk_container_add(GTK_CONTAINER(button_box), add_button);
    gtk_container_add(GTK_CONTAINER(button_box), remove_button);
    gtk_container_add(GTK_CONTAINER(button_box), start_button);
    gtk_container_add(GTK_CONTAINER(button_box), stop_button);

    gtk_widget_set_sensitive(stop_button, FALSE);

    g_signal_connect(add_button, "clicked", G_CALLBACK(add_process), NULL);
    g_signal_connect(remove_button, "clicked", G_CALLBACK(remove_process), NULL);
    g_signal_connect(start_button, "clicked", G_CALLBACK(start_watchdog), NULL);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(stop_watchdog), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
