// mini_http_server.c
// 实验 2 + 实验 3：最小可用的 HTTP 服务器（解析 GET 并按路径返回不同页面）

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define PORT        8080
#define BACKLOG     16
#define REQ_MAX     8192     // 最大读取请求字节（含头 + 可能的少量body）
#define LINE_MAX    2048

// 发送简单的文本/HTML响应
static void send_response(int fd, int status, const char *reason,
                          const char *content_type, const char *body) {
    char header[512];
    size_t body_len = body ? strlen(body) : 0;

    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Server: mini-c-http/0.1\r\n"
                     "\r\n",
                     status, reason, content_type, body_len);
    if (n < 0) return;

    (void)write(fd, header, (size_t)n);
    if (body_len > 0) (void)write(fd, body, body_len);
}

// 简单 HTML 片段
static void page_index(int fd) {
    const char *body =
        "<!doctype html><html><head><meta charset='utf-8'><title>Index</title></head>"
        "<body style='font-family: sans-serif'>"
        "<h1>Mini C HTTP Server</h1>"
        "<ul>"
        "<li><a href='/hello'>/hello</a></li>"
        "<li><a href='/time'>/time</a></li>"
        "</ul>"
        "</body></html>";
    send_response(fd, 200, "OK", "text/html; charset=utf-8", body);
}

static void page_hello(int fd) {
    const char *body =
        "<!doctype html><html><head><meta charset='utf-8'><title>Hello</title></head>"
        "<body style='font-family: sans-serif'>"
        "<h1>Hello from C!</h1>"
        "<p>这是 /hello 页面。</p>"
        "<p><a href='/'>返回首页</a></p>"
        "</body></html>";
    send_response(fd, 200, "OK", "text/html; charset=utf-8", body);
}

static void page_time(int fd) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char timestr[128] = {0};
    if (lt) strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S %Z", lt);

    char body[512];
    snprintf(body, sizeof(body),
        "<!doctype html><html><head><meta charset='utf-8'><title>Time</title></head>"
        "<body style='font-family: sans-serif'>"
        "<h1>当前时间</h1>"
        "<p>%s</p>"
        "<p><a href='/'>返回首页</a></p>"
        "</body></html>", timestr[0] ? timestr : "unknown");
    send_response(fd, 200, "OK", "text/html; charset=utf-8", body);
}

static void page_404(int fd, const char *path) {
    char body[512];
    snprintf(body, sizeof(body),
        "<!doctype html><html><head><meta charset='utf-8'><title>404</title></head>"
        "<body style='font-family: sans-serif'>"
        "<h1>404 Not Found</h1>"
        "<p>Path: %s</p>"
        "<p><a href='/'>返回首页</a></p>"
        "</body></html>", path);
    send_response(fd, 404, "Not Found", "text/html; charset=utf-8", body);
}

static void page_405(int fd, const char *method) {
    char body[256];
    snprintf(body, sizeof(body),
        "<!doctype html><html><head><meta charset='utf-8'><title>405</title></head>"
        "<body style='font-family: sans-serif'>"
        "<h1>405 Method Not Allowed</h1>"
        "<p>Method: %s</p>"
        "<p>Only GET is supported.</p>"
        "</body></html>", method);
    send_response(fd, 405, "Method Not Allowed", "text/html; charset=utf-8", body);
}

// 处理一个客户端连接
static void handle_client(int client_fd, struct sockaddr_in *peer) {
    (void)peer;

    char req[REQ_MAX];
    memset(req, 0, sizeof(req));

    // 读取请求（阻塞，简单做法）
    ssize_t total = 0;
    // 读取到头结束（\r\n\r\n）或缓冲满
    int header_end = -1;

    printf("yzt client http request:\n%s\n",client_fd);

    while (total < (ssize_t)sizeof(req)) {
        ssize_t n = read(client_fd, req + total, sizeof(req) - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return;
        }
        if (n == 0) break; // 对端关闭
        total += n;

        // 查找 \r\n\r\n
        for (ssize_t i = 3; i < total; ++i) {
            if (req[i-3]=='\r' && req[i-2]=='\n' && req[i-1]=='\r' && req[i]=='\n') {
                header_end = (int)i + 1;
                break;
            }
        }
        if (header_end >= 0) break;
        // 简化：不处理超长头；真实服务器应循环读取直至完整或超时
    }

    // 把读取到的内容打印出来（便于观察）
    printf("=== Incoming request (%zd bytes) ===\n%.*s\n", total,
           (int)((total < 1024) ? total : 1024), req); // 避免打印过长
    fflush(stdout);

    if (header_end < 0) {
        // 没有完整头，也回复一个 400
        send_response(client_fd, 400, "Bad Request", "text/html; charset=utf-8",
                      "<h1>400 Bad Request</h1>");
        return;
    }

    // 解析请求行（第一行）
    char *first_crlf = strstr(req, "\r\n");
    if (!first_crlf) {
        send_response(client_fd, 400, "Bad Request", "text/html; charset=utf-8",
                      "<h1>400 Bad Request</h1>");
        return;
    }

    char line[LINE_MAX];
    int line_len = (int)(first_crlf - req);
    if (line_len >= (int)sizeof(line)) line_len = (int)sizeof(line) - 1;
    memcpy(line, req, (size_t)line_len);
    line[line_len] = '\0';

    char method[16] = {0}, path[1024] = {0}, version[16] = {0};
    if (sscanf(line, "%15s %1023s %15s", method, path, version) != 3) {
        send_response(client_fd, 400, "Bad Request", "text/html; charset=utf-8",
                      "<h1>400 Bad Request</h1>");
        return;
    }

    // 记录解析结果
    printf("Parsed: method=%s, path=%s, version=%s\n", method, path, version);
    fflush(stdout);

    // 仅支持 GET
    if (strcmp(method, "GET") != 0) {
        page_405(client_fd, method);
        return;
    }

    // 一些浏览器会请求 /favicon.ico，这里直接返回 204
    if (strcmp(path, "/favicon.ico") == 0) {
        send_response(client_fd, 204, "No Content", "text/plain", "");
        return;
    }

    // 路由：根据 path 返回不同页面
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        page_index(client_fd);
    } else if (strcmp(path, "/hello") == 0) {
        page_hello(client_fd);
    } else if (strcmp(path, "/time") == 0) {
        page_time(client_fd);
    } else {
        page_404(client_fd, path);
    }
}

int main(void) {
    // 让 stdout 行缓冲（带换行立即刷新）
    setvbuf(stdout, NULL, _IOLBF, 0);
    // 避免客户端过早关闭导致 SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;     // 0.0.0.0
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen"); close(server_fd); return 1;
    }

    printf("HTTP server listening on http://0.0.0.0:%d\n", PORT);

    while (1) {
        struct sockaddr_in peer; socklen_t ps = sizeof(peer);
        int client_fd = accept(server_fd, (struct sockaddr*)&peer, &ps);
        if (client_fd < 0) { perror("accept"); continue; }
        handle_client(client_fd, &peer);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
