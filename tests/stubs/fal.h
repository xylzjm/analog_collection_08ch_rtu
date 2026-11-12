#ifndef FAL_STUB_H
#define FAL_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Host-test FAL initializer; one fake partition is always present. */
int fal_init(void);

#ifdef __cplusplus
}
#endif

#endif
