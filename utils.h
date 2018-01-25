/*
 *  utils.h
 *  Header file for general purpose routines
 */

#ifndef __LSUPNP_UTILS_H__
#define __LSUPNP_UTILS_H__

struct str_vector {
    char **str_array;
    int str_count;
};

/* Functions */
int str_vector_init(struct str_vector *vector);
int str_vector_add(struct str_vector *vector, char *str);
int str_vector_search(struct str_vector *vector, char *search_str);
int str_vector_free(struct str_vector *vector);

#endif  /* __LSUPNP_UTILS_H__ */

