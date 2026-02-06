//
// Unit tests for engine/prelude.h
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/prelude.h"

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, ...)                                                 \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            return;                                                            \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do                                                                         \
    {                                                                          \
        tests_run++;                                                           \
        printf("  %-50s", #fn);                                                \
        fn();                                                                  \
        tests_passed++;                                                        \
        printf("PASS\n");                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// Test allocator
// ---------------------------------------------------------------------------

static void* test_malloc(ptrdiff_t size, void* ctx)
{
    (void) ctx;
    return malloc(size);
}

static void test_free(void* ptr, void* ctx)
{
    (void) ctx;
    free(ptr);
}

static allocator test_alloc = { test_malloc, test_free, NULL };

// ---------------------------------------------------------------------------
// Tests: find_length_of_c_string
// ---------------------------------------------------------------------------

static void test_strlen_empty(void)
{
    TEST_ASSERT(find_length_of_c_string("") == 0, "expected 0 for empty");
}

static void test_strlen_normal(void)
{
    TEST_ASSERT(find_length_of_c_string("hello") == 5, "expected 5");
}

static void test_strlen_long(void)
{
    const char* s = "abcdefghijklmnopqrstuvwxyz";
    TEST_ASSERT(find_length_of_c_string(s) == 26, "expected 26");
}

// ---------------------------------------------------------------------------
// Tests: string_t / string_equal
// ---------------------------------------------------------------------------

static void test_make_str_literal(void)
{
    string_t s = MAKE_STR("hello");
    TEST_ASSERT(s.len == 5, "expected len 5, got %zu", s.len);
    TEST_ASSERT(memcmp(s.chars, "hello", 5) == 0, "chars mismatch");
}

static void test_make_from_c_string(void)
{
    string_t s = make_from_c_string("world");
    TEST_ASSERT(s.len == 5, "expected len 5");
    TEST_ASSERT(memcmp(s.chars, "world", 5) == 0, "chars mismatch");
}

static void test_string_equal_same(void)
{
    string_t a = MAKE_STR("test");
    string_t b = MAKE_STR("test");
    TEST_ASSERT(string_equal(a, b), "expected equal");
}

static void test_string_equal_different_len(void)
{
    string_t a = MAKE_STR("short");
    string_t b = MAKE_STR("longer");
    TEST_ASSERT(!string_equal(a, b), "expected not equal");
}

static void test_string_equal_different_content(void)
{
    string_t a = MAKE_STR("abc");
    string_t b = MAKE_STR("abd");
    TEST_ASSERT(!string_equal(a, b), "expected not equal");
}

static void test_string_equal_empty(void)
{
    string_t a = MAKE_STR("");
    string_t b = MAKE_STR("");
    TEST_ASSERT(string_equal(a, b), "empty strings should be equal");
}

// ---------------------------------------------------------------------------
// Tests: string_array_t
// ---------------------------------------------------------------------------

static void test_string_array_create(void)
{
    string_array_t* arr = make_string_array(&test_alloc, 4);
    TEST_ASSERT(arr != NULL, "allocation failed");
    TEST_ASSERT(arr->len == 0, "expected len 0");
    TEST_ASSERT(arr->cap == 4, "expected cap 4");
    string_array_free(&test_alloc, arr);
}

static void test_string_array_push_within_cap(void)
{
    string_array_t* arr = make_string_array(&test_alloc, 4);
    string_t        s   = MAKE_STR("hello");
    bool            ok  = string_array_push(&test_alloc, arr, s);
    TEST_ASSERT(ok, "push should succeed");
    TEST_ASSERT(arr->len == 1, "expected len 1");
    TEST_ASSERT(string_equal(arr->data[0], s), "data mismatch");
    string_array_free(&test_alloc, arr);
}

static void test_string_array_to_cstrings(void)
{
    string_array_t* arr = make_string_array(&test_alloc, 4);
    string_array_push(&test_alloc, arr, make_from_c_string("alpha"));
    string_array_push(&test_alloc, arr, make_from_c_string("beta"));

    const char** cstrs = string_array_to_cstrings(arr);
    TEST_ASSERT(cstrs != NULL, "expected non-null");
    TEST_ASSERT(strcmp(cstrs[0], "alpha") == 0, "first element mismatch");
    TEST_ASSERT(strcmp(cstrs[1], "beta") == 0, "second element mismatch");
    string_array_free(&test_alloc, arr);
}

static void test_string_array_to_cstrings_empty(void)
{
    string_array_t* arr = make_string_array(&test_alloc, 4);
    TEST_ASSERT(string_array_to_cstrings(arr) == NULL,
                "empty array should return NULL");
    string_array_free(&test_alloc, arr);
}

// ---------------------------------------------------------------------------
// Tests: list_t
// ---------------------------------------------------------------------------

static void test_list_create(void)
{
    list_t* list = make_list(&test_alloc, 8);
    TEST_ASSERT(list != NULL, "allocation failed");
    TEST_ASSERT(list->len == 0, "expected len 0");
    TEST_ASSERT(list->cap == 16, "expected cap 16 (2x)");
    test_alloc.free(list, test_alloc.ctx);
}

// ---------------------------------------------------------------------------
// Tests: clamp
// ---------------------------------------------------------------------------

static void test_clamp_within(void)
{
    TEST_ASSERT(clamp(5, 0, 10) == 5, "expected 5");
}

static void test_clamp_below(void)
{
    TEST_ASSERT(clamp(-3, 0, 10) == 0, "expected 0");
}

static void test_clamp_above(void)
{
    TEST_ASSERT(clamp(15, 0, 10) == 10, "expected 10");
}

static void test_clamp_at_min(void)
{
    TEST_ASSERT(clamp(0, 0, 10) == 0, "expected 0");
}

static void test_clamp_at_max(void)
{
    TEST_ASSERT(clamp(10, 0, 10) == 10, "expected 10");
}

// ---------------------------------------------------------------------------
// Tests: read_file / write_file round-trip
// ---------------------------------------------------------------------------

static void test_file_roundtrip(void)
{
    const char* path = "/tmp/zerus_test_roundtrip.bin";
    const char* data = "hello zerus engine";
    size_t      len  = strlen(data);

    bool wrote = write_file(path, data, len);
    TEST_ASSERT(wrote, "write_file failed");

    TEST_ASSERT(file_exists(path), "file should exist after write");

    string_t* content = read_file(&test_alloc, path);
    TEST_ASSERT(content != NULL, "read_file returned NULL");
    TEST_ASSERT(content->len == len,
                "length mismatch: got %zu expected %zu",
                content->len,
                len);
    TEST_ASSERT(memcmp(content->chars, data, len) == 0, "content mismatch");

    test_alloc.free(content, test_alloc.ctx);
    remove(path);
}

static void test_read_file_nonexistent(void)
{
    string_t* content
        = read_file(&test_alloc, "/tmp/zerus_no_such_file_12345.bin");
    TEST_ASSERT(content == NULL, "expected NULL for nonexistent file");
}

static void test_file_exists_nonexistent(void)
{
    TEST_ASSERT(!file_exists("/tmp/zerus_no_such_file_12345.bin"),
                "nonexistent file should return false");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    printf("Running prelude tests...\n");

    // find_length_of_c_string
    RUN_TEST(test_strlen_empty);
    RUN_TEST(test_strlen_normal);
    RUN_TEST(test_strlen_long);

    // string_t
    RUN_TEST(test_make_str_literal);
    RUN_TEST(test_make_from_c_string);
    RUN_TEST(test_string_equal_same);
    RUN_TEST(test_string_equal_different_len);
    RUN_TEST(test_string_equal_different_content);
    RUN_TEST(test_string_equal_empty);

    // string_array_t
    RUN_TEST(test_string_array_create);
    RUN_TEST(test_string_array_push_within_cap);
    RUN_TEST(test_string_array_to_cstrings);
    RUN_TEST(test_string_array_to_cstrings_empty);

    // list_t
    RUN_TEST(test_list_create);

    // clamp
    RUN_TEST(test_clamp_within);
    RUN_TEST(test_clamp_below);
    RUN_TEST(test_clamp_above);
    RUN_TEST(test_clamp_at_min);
    RUN_TEST(test_clamp_at_max);

    // file I/O
    RUN_TEST(test_file_roundtrip);
    RUN_TEST(test_read_file_nonexistent);
    RUN_TEST(test_file_exists_nonexistent);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? EXIT_SUCCESS : EXIT_FAILURE;
}
