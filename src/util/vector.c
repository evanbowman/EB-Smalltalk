
static bool ST_Vector_init(ST_Internal_Context *context, ST_Vector *vec,
                           ST_Size elemSize, ST_Size capacity) {
    vec->begin = malloc(elemSize * capacity);
    if (UNEXPECTED(!vec->begin))
        return false;
    vec->capacity = capacity;
    vec->end = vec->begin;
    vec->elemSize = elemSize;
    return true;
}

static ST_Size ST_Vector_len(ST_Vector *vec) {
    return (vec->end - vec->begin) / vec->elemSize;
}

static bool ST_Vector_push(ST_Internal_Context *context, ST_Vector *vec,
                           const void *element) {
    const ST_Size len = ST_Vector_len(vec);
    if (UNEXPECTED(len == vec->capacity)) {
        if (vec->capacity)
            vec->capacity = vec->capacity * 2;
        else
            vec->capacity = vec->elemSize;
        vec->begin = realloc(vec->begin, vec->capacity);
        if (!vec->begin)
            return false;
        vec->end = vec->begin + len;
    }
    memcpy(vec->end, element, vec->elemSize);
    vec->end += vec->elemSize;
    return true;
}

static void ST_Vector_pop(ST_Vector *vec) {
    assert(vec->end);
    vec->end -= vec->elemSize;
}

static void *ST_Vector_get(ST_Vector *vec, ST_Size index) {
    return vec->begin + index * vec->elemSize;
}

__attribute__((unused)) static void ST_Vector_free(ST_Vector *vec) {
    free(vec->begin);
}
