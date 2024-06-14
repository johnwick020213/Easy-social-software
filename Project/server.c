#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <mysql/mysql.h>

#define MAX_BUFF_SIZE 1000
#define MAX_CLIENTS 10
#define MAX_MSG_SIZE 1024
#define MAX_EVENTS 10

typedef struct {
    int fd;
    char username[MAX_BUFF_SIZE];
    int logged_in;
    struct sockaddr_in addr;
} Client;

MYSQL *mysql_conn;

void error_handling(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

#define DB_HOST "192.168.7.78"
#define DB_USER "root"
#define DB_PASS "020213"
#define DB_NAME "Project"

int db_connect() {
    mysql_conn = mysql_init(NULL);
    if (!mysql_real_connect(mysql_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "Database connection error: %s\n", mysql_error(mysql_conn));
        return 0;
    }
    printf("数据库连接成功\n");
    return 1;
}

int db_register(const char *username, const char *password, Client *clients, int num_clients) {
    // 检查是否已经有相同用户名登录
    for (int i = 0; i < num_clients; ++i) {
        if (clients[i].logged_in && strcmp(clients[i].username, username) == 0) {
            return -1; // 用户名已被注册
        }
    }

    // 插入用户数据到数据库
    char query[MAX_BUFF_SIZE];
    sprintf(query, "INSERT INTO users (username, password) VALUES ('%s', '%s')", username, password);
    if (mysql_query(mysql_conn, query)) {
        fprintf(stderr, "Query error: %s\n", mysql_error(mysql_conn));
        return 0; // 注册失败
    }

    printf("用户 %s 注册成功\n", username);
    return 1; // 注册成功
}

int db_login(const char *username, const char *password, Client *clients, int num_clients) {
    char query[MAX_BUFF_SIZE];
    MYSQL_RES *result;
    MYSQL_ROW row;

    for (int i = 0; i < num_clients; ++i) {
        if (clients[i].logged_in && strcmp(clients[i].username, username) == 0) {
            return -1;
        }
    }

    sprintf(query, "SELECT * FROM users WHERE username='%s' AND password='%s'", username, password);
    if (mysql_query(mysql_conn, query)) {
        fprintf(stderr, "Query error: %s\n", mysql_error(mysql_conn));
        return 0;
    }

    result = mysql_store_result(mysql_conn);
    if (result == NULL) {
        fprintf(stderr, "No result returned\n");
        return 0;
    }

    row = mysql_fetch_row(result);
    mysql_free_result(result);

    if (row) {
        printf("Login successful for user: %s\n", username);
        return 1;
    } else {
        printf("Login failed for user: %s\n", username);
        return 0;
    }
}

int main() {
    if (!db_connect()) {
        error_handling("Database connection error");
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        error_handling("socket error");
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        error_handling("setsockopt error");
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        error_handling("bind error");
    }

    if (listen(server_fd, 5) == -1) {
        error_handling("listen error");
    }

    printf("等待客户端连接...\n");

    Client clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].fd = -1;
        clients[i].logged_in = 0;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        error_handling("epoll_create1 error");
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        error_handling("epoll_ctl error");
    }

    while (1) {
        struct epoll_event events[MAX_EVENTS];
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {
            error_handling("epoll_wait error");
        }

        for (int i = 0; i < num_events; ++i) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
                if (client_fd == -1) {
                    error_handling("accept error");
                }

                printf("客户端 %s:%d 连接成功\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                event.events = EPOLLIN | EPOLLET;
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    error_handling("epoll_ctl error");
                }

                for (int j = 0; j < MAX_CLIENTS; ++j) {
                    if (clients[j].fd == -1) {
                        clients[j].fd = client_fd;
                        clients[j].addr = client_addr;
                        break;
                    }
                }
            } else {
                int client_fd = events[i].data.fd;
                char buffer[MAX_BUFF_SIZE];
                int recv_size = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                if (recv_size <= 0) {
                    for (int j = 0; j < MAX_CLIENTS; ++j) {
                        if (clients[j].fd == client_fd) {
                            printf("客户端 %s:%d 断开连接\n", inet_ntoa(clients[j].addr.sin_addr), ntohs(clients[j].addr.sin_port));
                            close(client_fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                            clients[j].fd = -1;
                            clients[j].logged_in = 0;
                            break;
                        }
                    }
                } else {
                    buffer[recv_size] = '\0';
                    int client_index = -1;
                    for (int j = 0; j < MAX_CLIENTS; ++j) {
                        if (clients[j].fd == client_fd) {
                            client_index = j;
                            break;
                        }
                    }

                    if (client_index == -1) {
                        printf("无法找到客户端信息\n");
                        continue;
                    }

                    printf("接收到来自客户端 %s 的消息：%s\n", inet_ntoa(clients[client_index].addr.sin_addr), buffer);

                    if (!clients[client_index].logged_in) {
                        if (strncmp(buffer, "login", 5) == 0) {
                            char username[MAX_BUFF_SIZE];
                            char password[MAX_BUFF_SIZE];
                            sscanf(buffer, "login %s %s", username, password);

                            int login_result = db_login(username, password, clients, MAX_CLIENTS);
                            if (login_result == 1) {
                                clients[client_index].logged_in = 1;
                                strcpy(clients[client_index].username, username);
                                send(client_fd, "login_success", strlen("login_success"), 0);
                            } else if (login_result == 0) {
                                send(client_fd, "login_fail", strlen("login_fail"), 0);
                            } else if (login_result == -1) {
                                send(client_fd, "already_logged_in", strlen("already_logged_in"), 0);
                            }
                        } else if (strncmp(buffer, "register", 8) == 0) {
                            char username[MAX_BUFF_SIZE];
                            char password[MAX_BUFF_SIZE];
                            sscanf(buffer, "register %s %s", username, password);
                            int register_result = db_register(username, password, clients, MAX_CLIENTS);
                            if (register_result == 1) {
                                send(client_fd, "register_success", strlen("register_success"), 0);
                                printf("用户 %s 注册成功\n", username);
                            } else if (register_result == 0) {
                                send(client_fd, "register_fail", strlen("register_fail"), 0);
                                printf("用户 %s 注册失败\n", username);
                            } else if (register_result == -1) {
                                send(client_fd, "already_registered", strlen("already_registered"), 0);
                                printf("用户名 %s 已经被注册\n", username);
                            }
                        }
                    } else {
                        // 处理聊天消息或其他操作
                        for (int j = 0; j < MAX_CLIENTS; ++j) {
                            if (clients[j].fd != -1 && clients[j].logged_in&&clients[j].fd!=client_fd) {
                                char msg_with_user[MAX_MSG_SIZE];
                                snprintf(msg_with_user, sizeof(msg_with_user), "[%s]: %s", clients[client_index].username, buffer);
                                ssize_t sent_size = send(clients[j].fd, msg_with_user, strlen(msg_with_user), 0);
                                if (sent_size == -1) {
                                    perror("send error");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    close(server_fd);
    mysql_close(mysql_conn);
    return 0;
}

