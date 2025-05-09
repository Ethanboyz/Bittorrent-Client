#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "tracker.h"
#include "bencode.h"

struct url_parts {
    char protocol[6];   // "http", "https", or "udp"
    char host[128];
    char path[128];
    char port[6];       // 80 for HTTP, 443 for HTTPS, or other
};

// encode binary data (info hash and peer id)
char *encode_bin_data(unsigned char *data, size_t len) {
    size_t max_len = len * 3 + 1;
    char *encoded = malloc(max_len);

    const char hex[] = "0123456789ABCDEF";
    int idx = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '~') {
            encoded[idx++] = c;
        } else {
            encoded[idx++] = '%';
            encoded[idx++] = hex[c >> 4];
            encoded[idx++] = hex[c & 0x0F];
        }
    }
    encoded[idx] = '\0';
    return encoded;
}

// parse announce URL into relevant parts to build GET request
void parse_announce(char *announce, struct url_parts *parts) {
    char *pos;

    if (strncmp(announce, "http://", 7) == 0) {
        strcpy(parts->port, "80");
        strcpy(parts->protocol, "http");
        pos = announce + 7;
    } else if (strncmp(announce, "https://", 8) == 0) {
        strcpy(parts->protocol, "https");
        strcpy(parts->port, "443");
        pos = announce + 8;
    } else if (strncmp(announce, "udp://", 6) == 0) {
        strcpy(parts->protocol, "udp");
        // TODO: UDP tracker support 
        pos = announce + 6;
    }

    // extract host
    char *slash_pos = strchr(pos, '/');
    size_t host_len;
    if (slash_pos) {
        host_len = slash_pos - pos;
    } else {
        host_len = strlen(pos);
    }
    strncpy(parts->host, pos, host_len);
    parts->host[host_len] = '\0';

    // check for port in host and extract if needed
    char *colon_pos = strchr(parts->host, ':');
    if (colon_pos) {
        strcpy(parts->port, colon_pos + 1);
        *colon_pos = '\0';
    }

    // extract path
    if (slash_pos) {
        size_t path_len = strlen(slash_pos);
        strncpy(parts->path, slash_pos, path_len);
        parts->path[path_len] = '\0';
    } else {
        strcpy(parts->path, "/");
    }
}

// TODO: fix handling response when it's chunked (HTTPS)
static size_t handle_chunked(const char *src, size_t src_len, char **out) {
    const char *p = src;
    const char *end = src + src_len;
    size_t total = 0;
    *out = malloc(src_len);
    char *dst = *out;

    while (p < end) {
        char *chunk;
        unsigned long size = strtoul(p, &chunk, 16);
        if (chunk == p || size == 0) {
            break;
        }

        p = chunk;
        if (p + 2 > end || p[0] != '\r' || p[1] != '\n') {
            break;
        }
        p += 2;

        if (p + size > end) {
            break;
        }
        memcpy(dst, p, size);
        dst   += size;
        total += size;

        p += size;
        if (p + 2 > end || p[0] != '\r' || p[1] != '\n') {
            break;
        }
        p += 2;
    }
    
    return total;
}

// parse bencoded tracker response
TrackerResponse parse_response(char *buf, size_t buf_len) {
    TrackerResponse response = {0};

    const char *data;
    for (size_t i = 0; i + 4 <= buf_len; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            data = buf + i + 4;
            break;
        }
    }
    size_t data_len = buf_len - (data - buf);

    bencode_t ben, ben_item;
    bencode_init(&ben, data, (int) data_len);
    const char *key;
    int key_len;

    while (bencode_dict_has_next(&ben)) {
        if (!bencode_dict_get_next(&ben, &ben_item, &key, &key_len)) {
            break;
        }
        if (key_len == 8 && strncmp(key, "interval", 8) == 0 && bencode_is_int(&ben_item)) {
            long interval;
            bencode_int_value(&ben_item, &interval);
            response.interval = interval;
        } else if (key_len == 8 && strncmp(key, "complete", 8) == 0 && bencode_is_int(&ben_item)) {
            long complete;
            bencode_int_value(&ben_item, &complete);
            response.complete = complete;
        } else if (key_len == 10 && strncmp(key, "incomplete", 10) == 0 && bencode_is_int(&ben_item)) {
            long incomplete;
            bencode_int_value(&ben_item, &incomplete);
            response.incomplete = incomplete;
        } else if (key_len == 5 && strncmp(key, "peers", 5) == 0 && bencode_is_string(&ben_item)) {
            const char *peers;
            int len;
            bencode_string_value(&ben_item, &peers, &len);
            int num_peers = len / 6;
            response.num_peers = num_peers;
            response.peers = calloc(num_peers, sizeof(Peer));
            for (int i = 0; i < num_peers; i++) {
                unsigned char *pos = (unsigned char *)(peers + (i * 6));
                uint32_t addr;
                memcpy(&addr, pos, sizeof(addr));
                response.peers[i].address = ntohl(addr);
                uint16_t port;
                memcpy(&port, pos + 4, sizeof(port));
                response.peers[i].port = ntohs(port);
            }
        }
    }

    return response;
}

// send HTTP or HTTPS GET request
TrackerResponse http_get(char *announce, unsigned char *info_hash, unsigned char *peer_id, 
        int port, int uploaded, int downloaded, int left) {
    char *encoded_hash = encode_bin_data(info_hash, 20);
    char *encoded_id = encode_bin_data(peer_id, 20);
    
    struct url_parts parts = {0};
    parse_announce(announce, &parts);

    // set parameters for request
    char params[1024];
    snprintf(params, sizeof(params), 
        "%s?info_hash=%s&peer_id=%s&port=%d&uploaded=%d&downloaded=%d&left=%d&compact=1",
        parts.path, encoded_hash, encoded_id, port, uploaded, downloaded, left);

    free(encoded_hash);
    free(encoded_id);

    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(parts.host, parts.port, &hints, &res);

    // create a socket and connect to tracker 
    int sock = socket(res->ai_family, SOCK_STREAM, res->ai_protocol);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(1);
    }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        perror("Connection failed");
        exit(1);
    }
    freeaddrinfo(res);

    // support for HTTPS tracker
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    if (strcmp(parts.protocol, "https") == 0) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        ctx = SSL_CTX_new(TLS_client_method());
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);
        if (!SSL_set_tlsext_host_name(ssl, parts.host)) {
            fprintf(stderr, "Failed to set SNI to %s\n", parts.host);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            exit(1);
        }
        if (SSL_connect(ssl) <= 0) {
            unsigned long err = ERR_get_error();                           
            fprintf(stderr, "SSL_connect error: %s\n", ERR_error_string(err, NULL)); 
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            exit(1);
        }
    }

    // build entire GET request
    char req[2048];
    int num = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        params, parts.host);

    if (ssl) {
        SSL_write(ssl, req, num);
    } else {
        send(sock, req, strlen(req), 0);
    }

    // receive request  
    size_t len = 0, max_len = 4096;
    char *res_buf = malloc(max_len);
    while (1) {
        int bytes_read;
        if (len >= max_len) {
            max_len *= 2;
            res_buf = realloc(res_buf, max_len);
        }
        if (ssl) {
            bytes_read = SSL_read(ssl, res_buf + len, max_len - len);
        } else {
            bytes_read = recv(sock, res_buf + len, max_len - len, 0);
        }
        if (bytes_read <= 0) {
            break;
        }
        len += bytes_read;
    }
    res_buf[len] = '\0';
    // printf("=== raw response ===\n%.*s\n", (int)len, res_buf);
    return parse_response(res_buf, len);
}

void free_tracker_response(TrackerResponse *response) {
    if (response->peers) {
        free(response->peers);
    }
    memset(response, 0, sizeof(*response));
}

// TODO: scrape request (this is more important to get done)

// TODO: UDP support