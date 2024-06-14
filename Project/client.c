#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <termios.h>
#include <sys/select.h>

#define MAX_BUFF_SIZE 1000

// 错误处理函数
void error_handling(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// 启用原始模式以处理退格键和即时显示字符
void enable_raw_mode() {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

// 禁用原始模式，恢复正常终端设置
void disable_raw_mode() {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

// 获取用户输入并处理退格键
void get_input(char *input, int size) {
    enable_raw_mode();
    int pos = 0;
    char ch;
    while (pos < size - 1) {
        read(STDIN_FILENO, &ch, 1);
        if (ch == '\n') {
            break;
        } else if (ch == 127) { // 处理退格键
            if (pos > 0) {
                pos--;
                printf("\b \b");
            }
        } else {
            input[pos++] = ch;
            printf("%c", ch);
        }
    }
    input[pos] = '\0';
    disable_raw_mode();
    printf("\n");
}

// 显示消息
void display_message(const char *message) {
    printf("%s\n", message);
}

int main() {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        error_handling("socket error");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("192.168.7.78"); // 假设服务器在本地

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        error_handling("connect error");
    }

    printf("连接成功\n");

    char buffer[MAX_BUFF_SIZE];
    char username[MAX_BUFF_SIZE];
    char password[MAX_BUFF_SIZE];
    int logged_in = 0;

    while (!logged_in) {
        printf("请输入命令：\n");
        printf("1. 登录\n");
        printf("2. 注册\n");
        printf("3. 退出\n");

        fgets(buffer, sizeof(buffer), stdin);
        buffer[strlen(buffer) - 1] = '\0'; // 去掉fgets读入的换行符

        if (strcmp(buffer, "1") == 0) {
            printf("请输入用户名：");
            fflush(stdout); // 确保提示信息立即显示
            get_input(username, sizeof(username));

            printf("请输入密码：");
            fflush(stdout); // 确保提示信息立即显示
            get_input(password, sizeof(password));

            sprintf(buffer, "login %s %s", username, password);
        } else if (strcmp(buffer, "2") == 0) {
            printf("请输入用户名：");
            fflush(stdout); // 确保提示信息立即显示
            get_input(username, sizeof(username));

            printf("请输入密码：");
            fflush(stdout); // 确保提示信息立即显示
            get_input(password, sizeof(password));

            sprintf(buffer, "register %s %s", username, password);
        } else if (strcmp(buffer, "3") == 0) {
            send(client_fd, "quit", strlen("quit"), 0);
            break;
        } else {
            printf("无效命令，请重新输入\n");
            continue;
        }

        send(client_fd, buffer, strlen(buffer), 0);
        memset(buffer, 0, sizeof(buffer));
        recv(client_fd, buffer, sizeof(buffer), 0);

        if (strncmp(buffer, "login_success", strlen("login_success")) == 0) {
            printf("登录成功\n");
            logged_in = 1;
        } else if (strncmp(buffer, "login_fail", strlen("login_fail")) == 0) {
            printf("登录失败，请检查用户名和密码\n");
        } else if (strncmp(buffer, "already_logged_in", strlen("already_logged_in")) == 0) {
            printf("登录失败，该账户已在其他地方登录\n");
            break;
        } else if (strncmp(buffer, "register_success", strlen("register_success")) == 0) {
            printf("注册成功，请登录\n");
        } else if (strncmp(buffer, "register_fail", strlen("register_fail")) == 0) {
            printf("注册失败，请重试\n");
        } else {
            printf("收到未知消息：%s\n", buffer);
        }
    }

    if (logged_in) {
        printf("欢迎进入聊天室\n");
        fd_set read_fds;
        while (1) {
            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);
            FD_SET(client_fd, &read_fds);

            int max_fd = client_fd;

            int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
            if (activity < 0) {
                error_handling("select error");
            }

            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                // 从标准输入读取数据并发送
                fgets(buffer, sizeof(buffer), stdin);
                printf("----------消息已发送-------------\n");
                buffer[strlen(buffer) - 1] = '\0'; // 去掉fgets读入的换行符
                if (strcmp(buffer, "quit") == 0) {
                    send(client_fd, "quit", strlen("quit"), 0);
                    break;
                }

                send(client_fd, buffer, strlen(buffer), 0);
            }

            if (FD_ISSET(client_fd, &read_fds)) {
                // 从服务器读取数据并显示
                int recv_size = recv(client_fd, buffer, sizeof(buffer), 0);
                if (recv_size <= 0) {
                    printf("与服务器的连接断开\n");
                    break;
                }
                buffer[recv_size] = '\0';
                
                printf("%s\n", buffer);
                printf("----------收到新消息-------------\n");
    }
        }}
    printf("退出客户端\n");
    close(client_fd);
    return 0;
}

