#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

// Simulate BITFIELD ID from your enum
#define BITFIELD 5

// Fake bitfield for testing
static uint8_t dummy_bitfield[] = { 0b10101010, 0b11110000 };

// Simulate the Peer struct
typedef struct {
    int dummy; // Not used
} Peer;

// These will capture what was sent
uint8_t captured_message[1024];
size_t captured_len = 0;

// Mocked version of piece_manager_get_our_bitfield
void piece_manager_get_our_bitfield(const uint8_t **bitfield, size_t *length) {
    *bitfield = dummy_bitfield;
    *length = sizeof(dummy_bitfield);
}

// Mocked version of send_message
int send_message(Peer *peer, const uint8_t *message, size_t message_len) {
    memcpy(captured_message, message, message_len);
    captured_len = message_len;
    return message_len;
}

// Actual function under test
int send_bitfield(Peer *peer) {
    const uint8_t *our_bitfield;
    size_t bitfield_length = 0;

    piece_manager_get_our_bitfield(&our_bitfield, &bitfield_length);

    if (bitfield_length == 0 || our_bitfield == NULL) {
        return 0;
    }

    uint8_t *message = malloc(5 + bitfield_length);
    if (!message) return -1;

    uint32_t length_prefix = htonl(1 + bitfield_length);
    memcpy(message, &length_prefix, 4);
    message[4] = BITFIELD;
    memcpy(message + 5, our_bitfield, bitfield_length);

    int result = send_message(peer, message, 5 + bitfield_length);
    free(message);
    return result == -1 ? -1 : 0;
}

// Main test
int main() {
    Peer dummy_peer = {0};
    int result = send_bitfield(&dummy_peer);

    printf("Result: %s\n", result == 0 ? "SUCCESS" : "FAILURE");

    // Print the message in hex
    printf("Sent %zu bytes: ", captured_len);
    for (size_t i = 0; i < captured_len; i++) {
        printf("%02X ", captured_message[i]);
    }
    printf("\n");

    return 0;
}
