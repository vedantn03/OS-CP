#ifndef RNG_UTILS_H
#define RNG_UTILS_H

void rng_init_thread(unsigned stream);
unsigned int rng_next(void);
int rng_uniform(int upper_bound);

#endif
