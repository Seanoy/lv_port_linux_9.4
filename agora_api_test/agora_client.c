#include <jansson.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HOST "api.agora.io"
#define PORT 443
#define BUFFER_SIZE 8192
#define APP_ID "a38a96a8b0674f79b17497c068cb24a8"
#define AUTH_HEADER                                                            \
  "ODc4YWIzNzk0MmRhNGNkNTg4YzYzNDMzYmE4M2IyZDM6NGI2MWU0YTViN2Q3NGE3NGE1Mjg4Ym" \
  "Q0YzM5ZWQyZTc="
#define BASE_PATH "/cn/api/conversational-ai-agent/v2/projects"

// 配置结构体
typedef struct {
  const char *host;
  int port;
  const char *app_id;
  const char *auth_header;
} Config;

// 请求结构体
typedef struct {
  const char *method;
  const char *path;
  const char *body;
  const char *content_type;
} HttpRequest;

// 响应结构体
typedef struct {
  int status_code;
  char *body;
  size_t body_length;
} HttpResponse;

// 全局配置
static Config config = {
    .host = HOST, .port = PORT, .app_id = APP_ID, .auth_header = AUTH_HEADER};

// JSON数据定义
const char *create_chat_ai_json_data =
    "{"
    "\"name\": \"convaiconsole_187941\","
    "\"properties\": {"
    "\"channel\": \"convaiconsole_187941\","
    "\"token\": "
    "\"007eJxTYFjw6PqBENGNjKufs+rFZWx1m+"
    "X0iesS016O26zWd4JTzpxQYEg0tki0NEu0SDIwMzdJM7dMMjQ3sTRPNjCzSE4yMkm0+"
    "LXcLLMhkJGhfIkvIyMDBIL4IgzJ+XlliZlAsjg/JzXe0MLc0sSQgQEAbmkklA==\","
    "\"agent_rtc_uid\": \"0\","
    "\"remote_rtc_uids\": [\"*\"],"
    "\"asr\": {\"language\": \"zh-CN\"},"
    "\"llm\": {"
    "\"url\": "
    "\"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions\","
    "\"api_key\": \"sk-83cf83736ea2427889845a089ea93370\","
    "\"system_messages\": [{\"role\": \"system\",\"content\": \"You are a "
    "helpful chatbot.\"}],"
    "\"greeting_message\": \"您好，有什么可以帮您？\","
    "\"failure_message\": \"抱歉，我无法回答这个问题。\","
    "\"max_history\": 10,"
    "\"params\": {\"model\": \"qwen-turbo\"}"
    "},"
    "\"tts\": {"
    "\"vendor\": \"bytedance\","
    "\"params\": {"
    "\"key\": \"2341650528\","
    "\"voice_id\": \"BV700_streaming\""
    "}"
    "}"
    "}"
    "}";

const char *change_param_json_data =
    "{"
    "\"properties\": {"
    "\"token\": "
    "\"007eJxTYFjw6PqBENGNjKufs+rFZWx1m+"
    "X0iesS016O26zWd4JTzpxQYEg0tki0NEu0SDIwMzdJM7dMMjQ3sTRPNjCzSE4yMkm0+"
    "LXcLLMhkJGhfIkvIyMDBIL4IgzJ+XlliZlAsjg/JzXe0MLc0sSQgQEAbmkklA==\","
    "\"llm\": {"
    "\"system_messages\": ["
    "{\"role\": \"system\",\"content\": \"You are a helpful assistant. xxx\"},"
    "{\"role\": \"system\",\"content\": \"Previously, user has talked about "
    "their favorite hobbies with some key topics: xxx\"}"
    "],"
    "\"params\": {"
    "\"model\": \"abab6.5s-chat\","
    "\"max_token\": 1024"
    "}"
    "}"
    "}"
    "}";

const char *speak_message_json_data =
    "{"
    "\"text\": \"你好，这是一条自定义播报消息。\","
    "\"priority\": \"INTERRUPT\","
    "\"interruptible\": true"
    "}";

const char *interrupt_agent_json_data = "{}";

// 存储agent_id
char agent_id[128] = "";

// 函数声明
SSL_CTX *create_ssl_context(void);
int create_socket(const char *hostname, int port);
HttpResponse *send_https_request(const HttpRequest *req, const char *agent_id);
void free_response(HttpResponse *response);
int parse_agent_id_from_json(const char *json_str);
int parse_response_json(const char *json_str);
int send_join_request(void);
int send_leave_request(void);
int send_update_request(void);
int send_get_status_request(void);
int send_get_list_request(void);

// 创建SSL上下文
SSL_CTX *create_ssl_context(void) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx) {
    fprintf(stderr, "Error creating SSL context\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  SSL_CTX_set_options(
      ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
  return ctx;
}

// 建立TCP连接
int create_socket(const char *hostname, int port) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("Error opening socket");
    return -1;
  }

  struct hostent *server = gethostbyname(hostname);
  if (!server) {
    fprintf(stderr, "Error: No such host\n");
    close(sockfd);
    return -1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
  server_addr.sin_port = htons(port);

  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("Error connecting");
    close(sockfd);
    return -1;
  }

  return sockfd;
}

// 构建HTTP请求字符串
char *build_http_request(const HttpRequest *req, const char *agent_id) {
  char *request = NULL;
  int length = 0;

  if (strcmp(req->method, "POST") == 0) {
    if (req->body) {
      length = snprintf(NULL, 0,
                        "%s %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Authorization: Basic %s\r\n"
                        "%s"  // Content-Type if present
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                        req->method, req->path, config.host, config.auth_header,
                        req->content_type ? req->content_type : "",
                        req->body ? strlen(req->body) : 0,
                        req->body ? req->body : "");
    } else {
      length =
          snprintf(NULL, 0,
                   "%s %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "Authorization: Basic %s\r\n"
                   "Content-Length: 0\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   req->method, req->path, config.host, config.auth_header);
    }
  } else {  // GET
    length = snprintf(NULL, 0,
                      "%s %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Authorization: Basic %s\r\n"
                      "Content-Length: 0\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      req->method, req->path, config.host, config.auth_header);
  }

  request = malloc(length + 1);
  if (!request) {
    return NULL;
  }

  if (strcmp(req->method, "POST") == 0 && req->body) {
    snprintf(request, length + 1,
             "%s %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "%s"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             req->method, req->path, config.host, config.auth_header,
             req->content_type ? req->content_type : "", strlen(req->body),
             req->body);
  } else if (strcmp(req->method, "POST") == 0) {
    snprintf(request, length + 1,
             "%s %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "Content-Length: 0\r\n"
             "Connection: close\r\n"
             "\r\n",
             req->method, req->path, config.host, config.auth_header);
  } else {
    snprintf(request, length + 1,
             "%s %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "Content-Length: 0\r\n"
             "Connection: close\r\n"
             "\r\n",
             req->method, req->path, config.host, config.auth_header);
  }

  return request;
}

// 从HTTP响应中提取JSON
char *extract_json_from_response(const char *response) {
  const char *json_start = strstr(response, "\r\n\r\n");
  if (!json_start) {
    json_start = strstr(response, "\n\n");
  }

  if (json_start) {
    json_start += 4;
    const char *brace = strchr(json_start, '{');
    if (brace) {
      return strdup(brace);
    }
  }

  return NULL;
}

// 统一的HTTPS请求发送函数
HttpResponse *send_https_request(const HttpRequest *req,
                                 const char *agent_id_for_path) {
  SSL_CTX *ctx = NULL;
  SSL *ssl = NULL;
  int sockfd = -1;
  HttpResponse *response = NULL;

  // 构建完整路径
  char full_path[1024];
  if (agent_id_for_path && strlen(agent_id_for_path) > 0) {
    snprintf(full_path, sizeof(full_path), req->path, config.app_id,
             agent_id_for_path);
  } else {
    snprintf(full_path, sizeof(full_path), req->path, config.app_id);
  }

  HttpRequest complete_req = {.method = req->method,
                              .path = full_path,
                              .body = req->body,
                              .content_type = req->content_type};

  // 构建HTTP请求
  char *http_request = build_http_request(&complete_req, agent_id_for_path);
  if (!http_request) {
    fprintf(stderr, "Failed to build HTTP request\n");
    return NULL;
  }

  // 创建SSL上下文和连接
  ctx = create_ssl_context();
  if (!ctx) goto cleanup;

  sockfd = create_socket(config.host, config.port);
  if (sockfd < 0) goto cleanup;

  ssl = SSL_new(ctx);
  if (!ssl) {
    fprintf(stderr, "Error creating SSL structure\n");
    goto cleanup;
  }

  SSL_set_fd(ssl, sockfd);
  if (SSL_connect(ssl) <= 0) {
    fprintf(stderr, "SSL connection failed\n");
    ERR_print_errors_fp(stderr);
    goto cleanup;
  }

  // 发送请求
  printf("Sending %s request to %s...\n", req->method, complete_req.path);
  int bytes_sent = SSL_write(ssl, http_request, strlen(http_request));
  if (bytes_sent <= 0) {
    fprintf(stderr, "SSL_write failed\n");
    goto cleanup;
  }
  printf("Request sent (%d bytes)\n\n", bytes_sent);

  // 接收响应
  response = malloc(sizeof(HttpResponse));
  if (!response) goto cleanup;

  response->body = NULL;
  response->body_length = 0;
  response->status_code = 0;

  char buffer[BUFFER_SIZE];
  ssize_t bytes_received;

  printf("Receiving response...\n");
  do {
    memset(buffer, 0, sizeof(buffer));
    bytes_received = SSL_read(ssl, buffer, sizeof(buffer) - 1);

    if (bytes_received > 0) {
      buffer[bytes_received] = '\0';
      printf("%s", buffer);

      // 解析状态码（从第一行）
      if (response->status_code == 0 && strstr(buffer, "HTTP/1.1") != NULL) {
        sscanf(buffer, "HTTP/1.1 %d", &response->status_code);
      }

      // 累积响应体
      char *new_body =
          realloc(response->body, response->body_length + bytes_received + 1);
      if (!new_body) {
        fprintf(stderr, "Memory allocation failed\n");
        free(response->body);
        free(response);
        response = NULL;
        goto cleanup;
      }
      response->body = new_body;
      memcpy(response->body + response->body_length, buffer, bytes_received);
      response->body_length += bytes_received;
      response->body[response->body_length] = '\0';
    }
  } while (bytes_received > 0);

  printf("\nResponse received (%zu bytes, status: %d)\n\n",
         response->body_length, response->status_code);

cleanup:
  free(http_request);

  if (ssl) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
  }

  if (sockfd >= 0) {
    close(sockfd);
  }

  if (ctx) {
    SSL_CTX_free(ctx);
  }

  return response;
}

// 释放响应内存
void free_response(HttpResponse *response) {
  if (response) {
    free(response->body);
    free(response);
  }
}

// 从JSON中解析agent_id
int parse_agent_id_from_json(const char *json_str) {
  json_error_t error;
  json_t *root = json_loads(json_str, 0, &error);

  if (!root) {
    fprintf(stderr, "Error parsing JSON: %s (line %d, column %d)\n", error.text,
            error.line, error.column);
    return -1;
  }

  json_t *agent_id_json = json_object_get(root, "agent_id");
  if (agent_id_json && json_is_string(agent_id_json)) {
    const char *id = json_string_value(agent_id_json);
    if (id) {
      strncpy(agent_id, id, sizeof(agent_id) - 1);
      agent_id[sizeof(agent_id) - 1] = '\0';
      printf("✓ Successfully parsed agent_id: %s\n", agent_id);
    }
  }

  json_decref(root);
  return 0;
}

// 解析响应JSON
int parse_response_json(const char *json_str) {
  printf("\nResponse JSON:\n%s\n\n", json_str);

  json_error_t error;
  json_t *root = json_loads(json_str, 0, &error);

  if (!root) {
    fprintf(stderr, "Error parsing JSON: %s\n", error.text);
    return -1;
  }

  // 可以在这里添加特定的JSON解析逻辑
  const char *key;
  json_t *value;
  json_object_foreach(root, key, value) {
    if (json_is_string(value)) {
      printf("  %s: %s\n", key, json_string_value(value));
    } else if (json_is_integer(value)) {
      printf("  %s: %lld\n", key, json_integer_value(value));
    } else if (json_is_boolean(value)) {
      printf("  %s: %s\n", key, json_is_true(value) ? "true" : "false");
    }
  }

  json_decref(root);
  return 0;
}

// 具体的API请求函数
int send_join_request(void) {
  HttpRequest req = {.method = "POST",
                     .path = BASE_PATH "/%s/join",
                     .body = create_chat_ai_json_data,
                     .content_type = "Content-Type: application/json\r\n"};

  HttpResponse *resp = send_https_request(&req, NULL);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    parse_agent_id_from_json(json);
    parse_response_json(json);
    free(json);
  }

  free_response(resp);
  return 0;
}

int send_leave_request(void) {
  if (strlen(agent_id) == 0) {
    printf("No agent_id available for leave request\n");
    return -1;
  }

  HttpRequest req = {.method = "POST",
                     .path = BASE_PATH "/%s/agents/%s/leave",
                     .body = NULL,
                     .content_type = NULL};

  HttpResponse *resp = send_https_request(&req, agent_id);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  printf("Leave request completed with status: %d\n", resp->status_code);
  free_response(resp);
  return 0;
}

int send_update_request(void) {
  if (strlen(agent_id) == 0) {
    printf("No agent_id available for update request\n");
    return -1;
  }

  HttpRequest req = {.method = "POST",
                     .path = BASE_PATH "/%s/agents/%s/update",
                     .body = change_param_json_data,
                     .content_type = "Content-Type: application/json\r\n"};

  HttpResponse *resp = send_https_request(&req, agent_id);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    parse_response_json(json);
    free(json);
  }

  free_response(resp);
  return 0;
}

int send_get_status_request(void) {
  if (strlen(agent_id) == 0) {
    printf("No agent_id available for get status request\n");
    return -1;
  }

  HttpRequest req = {.method = "GET",
                     .path = BASE_PATH "/%s/agents/%s",
                     .body = NULL,
                     .content_type = NULL};

  HttpResponse *resp = send_https_request(&req, agent_id);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    parse_response_json(json);
    free(json);
  }

  free_response(resp);
  return 0;
}

int send_get_list_request(void) {
  HttpRequest req = {.method = "GET",
                     .path = BASE_PATH "/%s/agents",
                     .body = NULL,
                     .content_type = NULL};

  HttpResponse *resp = send_https_request(&req, NULL);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    parse_response_json(json);
    free(json);
  }

  free_response(resp);
  return 0;
}

int send_speak_request(void) {
  if (strlen(agent_id) == 0) {
    printf("No agent_id available for speak request\n");
    return -1;
  }

  HttpRequest req = {.method = "POST",
                     .path = BASE_PATH "/%s/agents/%s/speak",
                     .body = speak_message_json_data,
                     .content_type = "Content-Type: application/json\r\n"};

  HttpResponse *resp = send_https_request(&req, agent_id);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    parse_response_json(json);
    free(json);
  }

  free_response(resp);
  return 0;
}

// 实现打断智能体请求函数
int send_interrupt_request(void) {
  if (strlen(agent_id) == 0) {
    printf("No agent_id available for interrupt request\n");
    return -1;
  }

  // 根据文档，打断接口的请求体是空对象 {}
  HttpRequest req = {.method = "POST",
                     .path = BASE_PATH "/%s/agents/%s/interrupt",
                     .body = interrupt_agent_json_data,
                     .content_type = "Content-Type: application/json\r\n"};

  HttpResponse *resp = send_https_request(&req, agent_id);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    printf("✓ Interrupt request successful\n");
    free(json);
  } else {
    printf("✗ No JSON response received\n");
  }

  free_response(resp);
  return 0;
}

// 实现获取智能体短期记忆请求函数
int send_get_history_request(void) {
  if (strlen(agent_id) == 0) {
    printf("No agent_id available for get history request\n");
    return -1;
  }

  HttpRequest req = {.method = "GET",
                     .path = BASE_PATH "/%s/agents/%s/history",
                     .body = NULL,
                     .content_type = NULL};

  HttpResponse *resp = send_https_request(&req, agent_id);
  if (!resp) return -1;

  // 检查响应状态码
  if (resp->status_code != 200) {
    printf("✗ request failed with status: %d\n", resp->status_code);
    free_response(resp);
    return -1;
  }

  char *json = extract_json_from_response(resp->body);
  if (json) {
    free(json);
  }

  free_response(resp);
  return 0;
}

int main() {
  int ret = 0;

  // 初始化OpenSSL
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  ERR_load_crypto_strings();

  printf("=== Starting Agora API Tests ===\n\n");

  // 1. 发送join请求
  printf("1. Sending JOIN request...\n");
  if (send_join_request() != 0) {
    printf("✗ JOIN request failed\n");
    ret = 1;
  }

  // 2. 播报自定义消息
  printf("\n2. Sending SPEAK request (custom message)...\n");
  if (send_speak_request() != 0) {
    printf("✗ SPEAK request failed\n");
    ret = 1;
  }

  // 发送interrupt请求
  printf("\n4. Sending INTERRUPT request...\n");
  if (send_interrupt_request() != 0) {
    printf("✗ INTERRUPT request failed\n");
    ret = 1;
  }
  printf("\n4. Sending GET HISTORY request...\n");
  if (send_get_history_request() != 0) {
    printf("✗ GET HISTORY request failed\n");
    ret = 1;
  }

  // 2. 发送get status请求
  printf("\n2. Sending GET STATUS request...\n");
  if (send_get_status_request() != 0) {
    printf("✗ GET STATUS request failed\n");
    ret = 1;
  }

  // 3. 发送get list请求
  printf("\n3. Sending GET LIST request...\n");
  if (send_get_list_request() != 0) {
    printf("✗ GET LIST request failed\n");
    ret = 1;
  }

  // 4. 发送update请求
  printf("\n4. Sending UPDATE request...\n");
  if (send_update_request() != 0) {
    printf("✗ UPDATE request failed\n");
    ret = 1;
  }

  // 5. 发送leave请求
  printf("\n5. Sending LEAVE request...\n");
  if (send_leave_request() != 0) {
    printf("✗ LEAVE request failed\n");
    ret = 1;
  }

  printf("\n=== All operations completed ===\n");
  if (strlen(agent_id) > 0) {
    printf("Final agent_id: %s\n", agent_id);
  }

  // 清理OpenSSL
  EVP_cleanup();
  ERR_free_strings();

  return ret;
}