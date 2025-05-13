#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <endian.h>

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
        // set a default UDP port??
        strcpy(parts->protocol, "udp");
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

// handle extra data/spaces when response is chunked 
size_t handle_chunked(const char *data, size_t data_len, char **dechunked) {
    const char *p = data;
    const char *end = data + data_len;
    size_t total = 0;
    *dechunked = malloc(data_len);
    char *dst = *dechunked;

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
    // set values to -1 if following data isn't given in response
    // (complete and incomplete values can be received from scrape request later)
    response.complete = -1;      
    response.incomplete = -1;

    const char *data;
    for (size_t i = 0; i + 4 <= buf_len; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            data = buf + i + 4;
            break;
        }
    }
    printf("data is %s\n", data);

    size_t data_len = buf_len - (data - buf);

    // handle case where response is chunked
    char *dechunked = NULL;
    size_t dechunked_len = 0;
    if (strstr(buf, "Transfer-Encoding: chunked")) {
        dechunked_len = handle_chunked(data, data_len, &dechunked);
        if (dechunked_len > 0) {
            data = dechunked;
            data_len = dechunked_len;
        }
    }

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
        // binary model peers
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
        // dictionary model peers
        } else if (key_len == 5 && strncmp(key, "peers", 5) == 0 && bencode_is_list(&ben_item)) {
            bencode_t peers = ben_item;
            int num_peers = 0;
            while (bencode_list_has_next(&peers)) {
                bencode_t peer;
                bencode_list_get_next(&peers, &peer);
                num_peers++;
            }
            response.num_peers = num_peers;
            response.peers = calloc(num_peers, sizeof(Peer));

            peers = ben_item;
            int i = 0;
            while (bencode_list_has_next(&peers)) {
                bencode_t peer;
                bencode_list_get_next(&peers, &peer);
                while (bencode_dict_has_next(&peer)) {
                    bencode_t field;
                    const char *field_key;
                    int field_key_len;
                    bencode_dict_get_next(&peer, &field, &field_key, &field_key_len);
                    // ip address
                    if (field_key_len == 2 && strncmp(field_key, "ip", 2) == 0 && bencode_is_string(&field)) {
                        const char *ip_string;
                        int ip_len;
                        bencode_string_value(&field, &ip_string, &ip_len);
                        char addr_buf[16] = {0};
                        memcpy(addr_buf, ip_string, ip_len);
                        struct in_addr addr;
                        inet_aton(addr_buf, &addr);
                        response.peers[i].address = ntohl(addr.s_addr);
                    }
                    // port
                    if (field_key_len == 4 && strncmp(field_key, "port", 4) == 0 && bencode_is_int(&field)) {
                        long port;
                        bencode_int_value(&field, &port);
                        response.peers[i].port = (uint16_t) port;
                    }
                }
                i++;
            }
        }
    }
    printf("interval is %d\n", response.interval);
    printf("incomplete is %d\n", response.incomplete);
    printf("complete is %d\n", response.complete);
    printf("number peers is %d\n", response.num_peers);

    free(dechunked);
    return response;
}

// send HTTP or HTTPS GET request
TrackerResponse http_get(struct url_parts *parts, unsigned char *info_hash, unsigned char *peer_id, 
        int port, long uploaded, long downloaded, long left) {
    char *encoded_hash = encode_bin_data(info_hash, 20);
    char *encoded_id = encode_bin_data(peer_id, 20);

    // set parameters for request
    char params[1024];
    snprintf(params, sizeof(params), 
        "%s?info_hash=%s&peer_id=%s&port=%d&uploaded=%ld&downloaded=%ld&left=%ld&compact=1",
        parts->path, encoded_hash, encoded_id, port, uploaded, downloaded, left);

    free(encoded_hash);
    free(encoded_id);

    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(parts->host, parts->port, &hints, &res);

    // create a socket and connect to tracker 
    int sock = socket(res->ai_family, SOCK_STREAM, res->ai_protocol);
    if (sock == -1) {
        perror("Socket creation failed");
        freeaddrinfo(res);
        exit(1);
    }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        perror("Connection failed");
        freeaddrinfo(res);
        exit(1);
    }
    freeaddrinfo(res);

    // support for HTTPS tracker
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    if (strcmp(parts->protocol, "https") == 0) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        ctx = SSL_CTX_new(TLS_client_method());
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);
        if (!SSL_set_tlsext_host_name(ssl, parts->host)) {
            fprintf(stderr, "Failed to set SNI to %s\n", parts->host);
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
        params, parts->host);

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

    // free sockets and SSL
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }
    close(sock);
    
    TrackerResponse resp = parse_response(res_buf, len);
    free(res_buf);
    return resp;
}

// TODO: set timeouts according to official protocol
// send GET request for UDP
TrackerResponse udp_get(struct url_parts *parts, unsigned char *info_hash, unsigned char *peer_id, 
        int port, long uploaded, long downloaded, long left) {
    
    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_DGRAM;
    getaddrinfo(parts->host, parts->port, &hints, &res);

    // create a socket and connect to tracker 
    int sock = socket(res->ai_family, SOCK_DGRAM, res->ai_protocol);
    if (sock == -1) {
        perror("Socket creation failed");
        freeaddrinfo(res);
        exit(1);
    }

    // send connect request
    uint8_t connect[16];
    uint64_t protocol_id = htobe64(0x41727101980);
    uint32_t action = htonl(0);
    uint32_t transaction_id = (uint32_t)rand();
    uint32_t transaction_id_be = htonl(transaction_id);
    memcpy(connect, &protocol_id, 8);
    memcpy(connect + 8, &action, 4);
    memcpy(connect + 12, &transaction_id_be, 4);

    if (sendto(sock, connect, 16, 0, res->ai_addr, res->ai_addrlen) != 16) {
        perror("Sending connect request failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }

    // receive connect response
    uint8_t connect_res[16];
    socklen_t addrlen = res->ai_addrlen;
    if (recvfrom(sock, connect_res, sizeof(connect_res), 0, res->ai_addr, &addrlen) < 16) {
        perror("Receiving connect request failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }

    uint32_t action_res = ntohl(*(uint32_t*)(connect_res));
    uint32_t transaction_id_res = ntohl(*(uint32_t*)(connect_res + 4));
    if (action_res != 0 || transaction_id_res != transaction_id) {
        fprintf(stderr, "Connect request failed (action=%d, transaction ID=%d)\n", 
            action_res, transaction_id_res);
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }
    uint64_t connection_id_be;
    memcpy(&connection_id_be, connect_res + 8, 8);    
    // uint64_t connection_id_host = be64toh(connection_id_be);

    // send announce request
    uint8_t announce[98];
    action = htonl(1);
    uint32_t trans_ann = rand();
    uint32_t trans_ann_be = htonl(trans_ann);
    uint64_t downloaded_be = htobe64((uint64_t) downloaded);
    uint64_t left_be = htobe64((uint64_t) left);
    uint64_t uploaded_be = htobe64((uint64_t) uploaded);
    uint32_t event = htonl(0);
    uint32_t ip_addr = htonl(0);
    uint32_t key = htonl((uint32_t) rand());
    uint32_t num_want = htonl(-1);
    uint16_t port_be = htons((uint16_t) port);
    memcpy(announce, &connection_id_be, 8);
    memcpy(announce + 8, &action, 4);
    memcpy(announce + 12, &trans_ann_be, 4);
    memcpy(announce + 16, info_hash, 20);
    memcpy(announce + 36, peer_id, 20);
    memcpy(announce + 56, &downloaded_be, 8);
    memcpy(announce + 64, &left_be, 8);
    memcpy(announce + 72, &uploaded_be, 8);
    memcpy(announce + 80, &event, 4);
    memcpy(announce + 84, &ip_addr, 4);
    memcpy(announce + 88, &key, 4);
    memcpy(announce + 92, &num_want, 4);
    memcpy(announce + 96, &port_be, 2);

    if (sendto(sock, announce, 98, 0, res->ai_addr, res->ai_addrlen) < 0) {
        perror("Sending announce request failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }

    // receive announce response
    uint8_t announce_res[1600];     // is buffer size okay??
    int bytes_read = recvfrom(sock, announce_res, sizeof(announce_res), 0, NULL, NULL);
    if (bytes_read < 20) {
        perror("Receiving announce request failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }

    // populate tracker response struct
    TrackerResponse resp = {0};
    uint32_t action_ann = ntohl(*(uint32_t*)(announce_res));
    uint32_t trans_id_ann = ntohl(*(uint32_t*)(announce_res + 4));
    if (action_ann != 1 || trans_id_ann != trans_ann) {
        fprintf(stderr, "Announce request failed (action=%d, transaction ID=%d)\n", 
            action_res, trans_id_ann);
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }
    uint32_t interval = ntohl(*(uint32_t*)(announce_res + 8));
    uint32_t incomplete = ntohl(*(uint32_t*)(announce_res + 12));
    uint32_t complete = ntohl(*(uint32_t*)(announce_res + 16));

    resp.interval = interval;
    resp.incomplete = incomplete;
    resp.complete = complete;

    int peers_len = bytes_read - 20;
    int num_peers = peers_len / 6;
    resp.num_peers = num_peers;
    resp.peers = calloc(num_peers, sizeof(Peer));
    for (int i = 0; i < num_peers; i++) {
        int offset = 20 + i * 6;
        uint32_t peer_addr;
        uint16_t peer_port;
        memcpy(&peer_addr, announce_res + offset, 4);
        memcpy(&peer_port, announce_res + offset + 4, 2);

        resp.peers[i].address = ntohl(peer_addr);
        resp.peers[i].port = ntohs(peer_port);
    }

    close(sock);
    freeaddrinfo(res);
    return resp;
}

TrackerResponse tracker_get(char *announce, unsigned char *info_hash, unsigned char *peer_id, 
        int port, long uploaded, long downloaded, long left) {
    struct url_parts parts = {0};
    parse_announce(announce, &parts);

    if (strcmp(parts.protocol, "udp") == 0) {
        return udp_get(&parts, info_hash, peer_id, port, uploaded, downloaded, left);
    } else {
        return http_get(&parts, info_hash, peer_id, port, uploaded, downloaded, left);
    }
}

void parse_scrape_response(TrackerResponse *response, char *buf, size_t buf_len) {
    const char *data;
    for (size_t i = 0; i + 4 <= buf_len; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            data = buf + i + 4;
            break;
        }
    }
    size_t data_len = buf_len - (data - buf);

    // handle case where response is chunked
    char *dechunked = NULL;
    size_t dechunked_len = 0;
    if (strstr(buf, "Transfer-Encoding: chunked")) {
        dechunked_len = handle_chunked(data, data_len, &dechunked);
        if (dechunked_len > 0) {
            data = dechunked;
            data_len = dechunked_len;
        }
    }

    bencode_t ben, ben_item;
    bencode_init(&ben, data, (int) data_len);
    const char *key;
    int key_len;
    while (bencode_dict_has_next(&ben)) {
        bencode_dict_get_next(&ben, &ben_item, &key, &key_len);
        if (key_len == 5 && strncmp(key, "files", 5) == 0 && bencode_is_dict(&ben_item)) {
            bencode_t files = ben_item;
            while (bencode_dict_has_next(&files)) {
                bencode_t info_entry;
                const char *info_key;
                int info_len;
                bencode_dict_get_next(&files, &info_entry, &info_key, &info_len);
                bencode_t stats = info_entry;
                while (bencode_dict_has_next(&stats)) {
                    bencode_t field;
                    const char *field_key;
                    int field_key_len;

                    bencode_dict_get_next(&stats, &field, &field_key, &field_key_len);
                    if (field_key_len == 8 && strncmp(field_key, "complete", 8) == 0 && bencode_is_int(&field)) {
                        long complete;
                        bencode_int_value(&field, &complete);
                        response->complete = complete;
                    } else if (field_key_len == 10 && strncmp(field_key, "incomplete", 10) == 0 && bencode_is_int(&field)) {
                        long incomplete;
                        bencode_int_value(&field, &incomplete);
                        response->incomplete = incomplete;
                    }
                }
            }
        }
        break;
    }
    free(dechunked);
}

// tracker scrape convention for HTTP(S)
int http_scrape(TrackerResponse *response, struct url_parts *parts, unsigned char *info_hash) {
    char scrape_path[128];
    strncpy(scrape_path, parts->path, sizeof(scrape_path));
    char *to_replace = strstr(scrape_path, "announce");
    // scrape convention is not supported 
    if (!to_replace) {
        return -1;
    }

    memmove(to_replace + strlen("scrape"), to_replace + 8, strlen(to_replace + 8) + 1);
    memcpy(to_replace, "scrape", strlen("scrape"));

    // set parameters for request
    char *encoded_hash = encode_bin_data(info_hash, 20);
    char params[1024];
    snprintf(params, sizeof(params), "%s?info_hash=%s", scrape_path, encoded_hash);
    free(encoded_hash);

    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(parts->host, parts->port, &hints, &res);

    // create a socket and connect to tracker 
    int sock = socket(res->ai_family, SOCK_STREAM, res->ai_protocol);
    if (sock == -1) {
        perror("Socket creation failed");
        freeaddrinfo(res);
        return -1;
    }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        perror("Connection failed");
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    // support for HTTPS tracker
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    if (strcmp(parts->protocol, "https") == 0) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        ctx = SSL_CTX_new(TLS_client_method());
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);
        if (!SSL_set_tlsext_host_name(ssl, parts->host)) {
            fprintf(stderr, "Failed to set SNI to %s\n", parts->host);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            return -1;
        }
        if (SSL_connect(ssl) <= 0) {
            unsigned long err = ERR_get_error();                           
            fprintf(stderr, "SSL_connect error: %s\n", ERR_error_string(err, NULL)); 
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            return -1;
        }
    }

    // build entire GET request
    char req[2048];
    int num = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        params, parts->host);

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

    // free sockets and SSL
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }
    close(sock);
    
    // parse response here 
    parse_scrape_response(response, res_buf, len);
    free(res_buf);
    return 0;
}

// tracker scrape convention for UDP
// TODO: make connection request/response cleaner
int udp_scrape(TrackerResponse *response, struct url_parts *parts, unsigned char *info_hash) {
    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_DGRAM;
    getaddrinfo(parts->host, parts->port, &hints, &res);

    // create a socket and connect to tracker 
    int sock = socket(res->ai_family, SOCK_DGRAM, res->ai_protocol);
    if (sock == -1) {
        perror("Socket creation failed");
        freeaddrinfo(res);
        exit(1);
    }

    // send connect request
    uint8_t connect[16];
    uint64_t protocol_id = htobe64(0x41727101980);
    uint32_t action = htonl(0);
    uint32_t transaction_id = (uint32_t)rand();
    uint32_t transaction_id_be = htonl(transaction_id);
    memcpy(connect, &protocol_id, 8);
    memcpy(connect + 8, &action, 4);
    memcpy(connect + 12, &transaction_id_be, 4);

    if (sendto(sock, connect, 16, 0, res->ai_addr, res->ai_addrlen) != 16) {
        perror("Sending connect request failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }

    // receive connect response
    uint8_t connect_res[16];
    socklen_t addrlen = res->ai_addrlen;
    if (recvfrom(sock, connect_res, sizeof(connect_res), 0, res->ai_addr, &addrlen) < 16) {
        perror("Receiving connect request failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }
    uint32_t action_res = ntohl(*(uint32_t*)(connect_res));
    uint32_t transaction_id_res = ntohl(*(uint32_t*)(connect_res + 4));
    if (action_res != 0 || transaction_id_res != transaction_id) {
        fprintf(stderr, "Connect request failed (action=%d, transaction ID=%d)\n", 
            action_res, transaction_id_res);
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }
    uint64_t connection_id_be;
    memcpy(&connection_id_be, connect_res + 8, 8);
    // uint64_t connection_id_host = be64toh(connection_id_be);

    // send scrape request
    uint8_t scrape[16 + 20];
    action = htonl(2);
    uint32_t scrape_trans_id = rand();
    uint32_t scrape_trans_id_be = htonl(scrape_trans_id);
    memcpy(scrape, &connection_id_be, 8);
    memcpy(scrape + 8, &action, 4);
    memcpy(scrape + 12, &scrape_trans_id_be, 4);
    memcpy(scrape + 16, info_hash, 20);

    if (sendto(sock, scrape, 16 + 20, 0, res->ai_addr, res->ai_addrlen) < 0) {
        perror("Sending scrape request failed");
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    // receive scrape response
    uint8_t resp[16 + 12];
    if (recvfrom(sock, resp, 16 + 12, 0, NULL, NULL) < 8) {
        perror("Receiving scrape response failed");
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
    
    action_res = ntohl(*(uint32_t*)(resp));
    transaction_id_res = ntohl(*(uint32_t*)(resp + 4));
    if (action_res != 2 || transaction_id_res != scrape_trans_id) {
        fprintf(stderr, "Scrape response failed (action=%u tx=%u)\n", action_res, transaction_id_res);
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    uint32_t complete = ntohl(*(uint32_t*)(resp + 8));
    uint32_t incomplete  = ntohl(*(uint32_t*)(resp + 16));

    response->complete = complete;
    response->incomplete = incomplete;
    close(sock);
    freeaddrinfo(res);
    return 0;
}

int scrape(TrackerResponse *response, char *announce, unsigned char *info_hash) {
    struct url_parts parts = {0};
    parse_announce(announce, &parts);

    if (strcmp(parts.protocol, "udp") == 0) {
        return udp_scrape(response, &parts, info_hash);
    } else {
        return http_scrape(response, &parts, info_hash);
    }
}

void free_tracker_response(TrackerResponse *response) {
    if (response->peers) {
        free(response->peers);
    }
    memset(response, 0, sizeof(*response));
}