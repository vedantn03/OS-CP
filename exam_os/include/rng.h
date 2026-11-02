#ifndef RNG_UTILS_H
#define RNG_UTILS_H

void rng_seed_thread(unsigned int salt);
unsigned int rng_next(void);
int rng_uniform(int upper_bound);

#endif
