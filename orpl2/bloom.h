#ifndef BLOOM_H
#define BLOOM_H

/*
 *
 * m: the size of the bloom filter in bits
 * n: number of entries (elements inserted)
 * k: number of hashes
 *
 * We have a max filter size of 864
 * Each hash must be of size log2(864) = 10 bits
 *
 * We optimize the filter for nodes having all Indriya nodes in their set, i.e. 138 elements.
 * m/n = 864/138 ~= 6.26
 * In case m=512:
 * m/n = 512/138 = 4
 *
 * False-positive rate for an optimal k is p = exp(-(m/n)*log(2)**2)
 * False positive rates for various m/n (number of bit per entry):
 *    m/n ==  1 => 61.9 %
 *    m/n ==  2 => 38.3 %
 *    m/n ==  4 => 14.6 %
 *    m/n ==  6 =>  5.5 %
 *    m/n ==  8 =>  2.1 %
 *    m/n == 10 =>  0.8 %
 *    m/n == 12 =>  0.3 %
 *    m/n == 14 =>  0.12 %
 *    m/n == 16 =>  0.05 %
 *
 *    m/n = 864/138 => 4.93 % <= our false positive rate assuming perfect k
 *
 * Optimal number of hash functions, k, is: (m/n)*log(2)
 *   m/n = 864/138 => ~ 4.33
 *   m/n = 768/138 => ~ 3.85
 *   m/n = 640/138 => ~ 3.21
 *   m/n = 512/138 => ~ 2.57
 *   m/n = 384/138 => ~ 1.92
 *   m/n = 256/138 => ~ 1.28
 *   m/n = 128/138 => ~ 0.6
 *
 * We use a k of 4 in case m = 864
 * We use a k of 3 in case m = 512
 * We need 3 or 4 hashes of 10 bits. A 64-bit hash truncated in 3 or 4 parts will do the job.
 *
 * False positive rate with this k=4: p = (1 - e**(-k*n/m))**k
 *    m/n ==  1 => 92.87 %
 *    m/n ==  2 => 55.89 %
 *    m/n ==  4 => 15.97 %
 *    m/n ==  6 =>  5.61 %
 *    m/n ==  8 =>  2.40 %
 *    m/n == 10 =>  1.18 %
 *    m/n == 12 =>  0.6 %
 *    m/n == 14 =>  0.38 %
 *    m/n == 16 =>  0.24 %
 *
 *    m/n = 864/138 => 4.97 % <= our false positive rate with m = 864 and k = 4
 *    m/n = 512/138 => 17.05 % <= our false positive rate with m = 512 and k = 3
 *
 */

#define BLOOM_FILTER_SIZE          BLOOM_M   /* Bloom filter size (in bits) */

#if BLOOM_FILTER_SIZE > 848
#error BLOOM_FILTER_SIZE greater than 848
#elif BLOOM_FILTER_SIZE > 512
#define BLOOM_HASH_SHIFT 10
#elif BLOOM_FILTER_SIZE > 256
#define BLOOM_HASH_SHIFT 9
#else
#define BLOOM_HASH_SHIFT 8
#endif

/* 108 bytes (864 bits) is the max we manage to fit in our 15.4 payload */
#if BLOOM_K == 0
#define OMNISCIENT_BLOOM 1
#define BLOOM_FILTER_K                      1   /* Number of hashs */
#else
#define OMNISCIENT_BLOOM 0
#define BLOOM_FILTER_K                      BLOOM_K   /* Number of hashs */
#endif

/* We implement aging through double-buffering */
typedef unsigned char bloom_filter[BLOOM_FILTER_SIZE / 8];
typedef struct {
  bloom_filter filters[2]; /* The two filters: one active and one inactive. */
  int current; /* Index of the currently active filter. 1-current is the inactive one. */
  int insert_count_current;
  int insert_count_warmup;
} double_bf;

void bloom_init(double_bf *dbf);
void bloom_insert(double_bf *dbf, unsigned char *ptr, int size);
void bloom_merge(double_bf *dbf, bloom_filter bf, uint16_t id);
int bloom_contains(double_bf *dbf, unsigned char *ptr, int size);
void bloom_swap(double_bf *dbf);
void bloom_print(double_bf *dbf);
int bloom_count_bits(double_bf *dbf);

#endif
