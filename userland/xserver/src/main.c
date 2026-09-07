/*
 * SzpontOS - SzpontX11 Native X11 Server
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Main Server Entry Point, IPC Sockets & Polling Event Loop
 */

#include "xserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>

server_t g_server;

static void handle_signal(int sig) {
    (void)sig;
    g_server.running = false;
}

static int create_unix_socket(int display_num) {
    mkdir("/tmp/.X11-unix", 0777);

    char path[108];
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", display_num);
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[SzpontX11] socket(AF_UNIX) failed");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[SzpontX11] bind(AF_UNIX) failed");
        close(fd);
        return -1;
    }

    if (listen(fd, 16) < 0) {
        perror("[SzpontX11] listen(AF_UNIX) failed");
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    chmod(path, 0777);
    printf("[SzpontX11] Listening on UNIX domain socket: %s (FD %d)\n", path, fd);
    return fd;
}

static int create_tcp_socket(int display_num) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[SzpontX11] socket(AF_INET) failed");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)(6000 + display_num));

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[SzpontX11] bind(TCP) failed");
        close(fd);
        return -1;
    }

    if (listen(fd, 16) < 0) {
        perror("[SzpontX11] listen(TCP) failed");
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    printf("[SzpontX11] Listening on TCP port: %d (FD %d)\n", 6000 + display_num, fd);
    return fd;
}

int main(int argc, char *argv[]) {
    int display_num = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == ':' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            display_num = atoi(&argv[i][1]);
        }
    }

    printf("\n========================================================\n");
    printf("  SzpontX11 - Native X11 Graphical Display Server\n");
    printf("  (C) Copyright by Szpont Industries. All rights reserved.\n");
    printf("  Target: Higher-Half Ring 3 / Display :%d\n", display_num);
    printf("========================================================\n\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    memset(&g_server, 0, sizeof(g_server));
    g_server.running = true;

    /* 1. Initialize DRM/KMS Hardware Display */
    if (!drm_init_display()) {
        fprintf(stderr, "[SzpontX11] Fatal: Failed to initialize DRM/KMS hardware display!\n");
        return 1;
    }

    /* 2. Initialize Subsystems */
    atom_init_system();
    window_init_system();
    client_init_system();
    input_init();
    draw_init_system();

    /* 3. Setup Sockets */
    g_server.unix_listen_fd = create_unix_socket(display_num);
    g_server.tcp_listen_fd  = create_tcp_socket(display_num);

    if (g_server.unix_listen_fd < 0 && g_server.tcp_listen_fd < 0) {
        fprintf(stderr, "[SzpontX11] Fatal: Could not create any listening sockets!\n");
        return 1;
    }

    /* 4. Draw initial scene and desktop */
    draw_composite_scene();
    printf("[SzpontX11] Server initialized and ready for X11 clients!\n\n");

    /* 5. Main Polling Loop */
    while (g_server.running) {
        struct pollfd fds[MAX_CLIENTS + 8];
        int num_fds = 0;

        /* Listening sockets */
        if (g_server.unix_listen_fd >= 0) {
            fds[num_fds].fd = g_server.unix_listen_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }
        if (g_server.tcp_listen_fd >= 0) {
            fds[num_fds].fd = g_server.tcp_listen_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }

        /* Input devices */
        if (g_server.mouse_fd >= 0) {
            fds[num_fds].fd = g_server.mouse_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }
        if (g_server.kbd_fd >= 0) {
            fds[num_fds].fd = g_server.kbd_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }
        if (g_server.mice_fd >= 0) {
            fds[num_fds].fd = g_server.mice_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }
        if (g_server.psaux_fd >= 0) {
            fds[num_fds].fd = g_server.psaux_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }
        if (g_server.devmouse_fd >= 0) {
            fds[num_fds].fd = g_server.devmouse_fd;
            fds[num_fds].events = POLLIN;
            fds[num_fds].revents = 0;
            num_fds++;
        }

        /* Connected X11 clients */
        client_t *fd_client_map[MAX_CLIENTS + 8] = {0};
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_server.clients[i].active && g_server.clients[i].fd >= 0) {
                fd_client_map[num_fds] = &g_server.clients[i];
                fds[num_fds].fd = g_server.clients[i].fd;
                fds[num_fds].events = POLLIN;
                fds[num_fds].revents = 0;
                num_fds++;
            }
        }

        int ret = poll(fds, (nfds_t)num_fds, 16); /* ~60 Hz poll interval */
        if (ret < 0) {
            continue;
        }

        /* Process input events first */
        input_process_events();

        /* Process socket events */
        for (int i = 0; i < num_fds; i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == g_server.unix_listen_fd || fds[i].fd == g_server.tcp_listen_fd) {
                    client_t *c = client_accept(fds[i].fd);
                    if (c) {
                        client_process_data(c);
                    }
                } else if (fd_client_map[i]) {
                    client_process_data(fd_client_map[i]);
                }
            }
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (fd_client_map[i] && fd_client_map[i]->active) {
                    client_close(fd_client_map[i]);
                }
            }
        }

        if (g_server.needs_redraw) {
            draw_composite_scene();
            g_server.needs_redraw = false;
        }
    }

    printf("[SzpontX11] Shutting down SzpontX11 server...\n");
    input_cleanup();
    drm_cleanup_display();
    if (g_server.unix_listen_fd >= 0) close(g_server.unix_listen_fd);
    if (g_server.tcp_listen_fd >= 0) close(g_server.tcp_listen_fd);

    char path[108];
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", display_num);
    unlink(path);

    printf("[SzpontX11] Server terminated cleanly.\n");
    return 0;
}
