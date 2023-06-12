#pragma once

/*
 * Adapted from Adapted from: https://github.com/Ginurx/chacha20-c
 */

struct chacha_context
{
	uint32_t keystream32[16];
	size_t position;

	uint8_t key[32];
	uint8_t nonce[12];
	uint64_t counter;

	uint32_t state[16];
};

void chacha_init_context(struct chacha_context *ctx, uint8_t key[], uint8_t nounc[], uint64_t counter);

void chacha_xor(struct chacha_context *ctx, uint8_t *bytes, size_t n_bytes);
