//
// Created by akash on 28/6/25.
//

#ifndef PRELUDE_H
#define PRELUDE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct
{
    void* (*malloc)(ptrdiff_t, void* ctx);
    void (*free)(void*, void* ctx);
    void* ctx;
} allocator;

typedef enum
{
    ALLOC_FAILED
} prelude_err;

typedef struct
{
    const char* chars;
    size_t      len;
} string_t;

typedef struct string_array_t
{
    size_t   len;
    size_t   cap;
    string_t data[];
} string_array_t;


#define MAKE_STR(literal) { .chars = (literal), .len = sizeof(literal) - 1 }


size_t find_length_of_c_string(const char str[])
{
    size_t len = 0;
    while (str[len] != '\0')
    {
        len++;
    }

    return len;
}

string_t make_from_c_string(const char* str)
{
    return (string_t) { .chars = str, .len = find_length_of_c_string(str) };
}

bool string_equal(string_t lhs, string_t rhs)
{
    if (lhs.len != rhs.len)
    {
        return false;
    }

    for (size_t i = 0; i < lhs.len; i++)
    {
        if (lhs.chars[i] != rhs.chars[i])
        {
            return false;
        }
    }

    return true;
}

string_array_t* make_string_array(allocator* alloc, size_t cap)
{
    size_t size = sizeof(string_array_t) + cap * sizeof(string_t);

    string_array_t* arr = alloc->malloc(size, alloc->ctx);
    if (!arr)
    {
        return NULL;
    }

    arr->len = 0;
    arr->cap = cap;

    return arr;
}

bool string_array_push(allocator* alloc, string_array_t* arr, string_t str)
{
    if (!arr)
    {
        return false;
    }

    // Check if we need to grow the array
    if (arr->len >= arr->cap)
    {
        size_t new_cap  = arr->cap == 0 ? 8 : arr->cap * 2;
        size_t new_size = sizeof(string_array_t) + new_cap * sizeof(string_t);
        string_array_t* new_arr = alloc->malloc(new_size, alloc->ctx);
        if (!new_arr)
        {
            return false;
        }

        new_arr->len = arr->len;
        new_arr->cap = new_cap;

        // copy existing data
        for (size_t i = 0; i < arr->len; i++)
        {
            new_arr->data[i] = arr->data[i];
        }

        // Free old array
        alloc->free(arr, alloc->ctx);
        arr = new_arr;
    }

    // Add the new string
    arr->data[arr->len++] = str;
    return true;
}

void string_array_free(allocator* alloc, string_array_t* arr)
{
    if (!arr)
    {
        return;
    }

    alloc->free(arr, alloc->ctx);
}

// Convert string_array_t to const char** for C APIs like Vulkan
// Returns a contiguous array of char pointers for cache efficiency
const char** string_array_to_cstrings(const string_array_t* arr)
{
    if (!arr || arr->len == 0)
    {
        return NULL;
    }

    // Use a static buffer to avoid heap allocation for better performance
    // This assumes reasonable limits for typical use cases
    static const char* buffer[256];

    if (arr->len > 256)
    {
        return NULL;  // Safety check
    }

    for (size_t i = 0; i < arr->len; i++)
    {
        buffer[i] = arr->data[i].chars;
    }

    return buffer;
}

// Get const char* from string_t
const char* string_to_cstring(const string_t* str)
{
    return str ? str->chars : NULL;
}

typedef struct
{
    size_t len;
    size_t cap;
    void*  data[];
} list_t;

list_t* make_list(allocator* alloc, size_t len)
{
    // allocate twice the capacity, so if it grows
    // we dont have to re-allocate right away
    size_t buffer = 2;

    size_t  size     = sizeof(list_t) + len * sizeof(void*) * buffer;
    list_t* new_list = alloc->malloc(size, alloc->ctx);
    if (!new_list)
    {
        return nullptr;
    }

    new_list->len = 0;
    new_list->cap = len * 2;

    return new_list;
}

int clamp(int d, int min, int max)
{
    const int t = d < min ? min : d;
    return t > max ? max : t;
}

// Read file data to string_t
__attribute__((unused)) static string_t* read_file(allocator*  alloc,
                                                   const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        return NULL;
    }

    // Seek to the end of the file to determine its size.
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 0)
    {
        fclose(file);
        return NULL;
    }

    // Allocate one contiguous memory block for the header, the string_t view,
    // the file content, and a null terminator. This makes cleanup trivial.
    size_t    total_size = sizeof(string_t) + file_size + 1;
    string_t* data       = alloc->malloc(total_size, alloc->ctx);
    if (!data)
    {
        fclose(file);
        return NULL;
    }

    data->len = file_size;

    // The character buffer starts immediately after the string_t element in our
    // single block.
    char* buffer = (char*) (data + 1);

    // The final string_t will still hold a const pointer, providing read-only
    // access.
    data->chars = buffer;

    // Read the entire file into our allocated buffer.
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t) file_size)
    {
        // If we couldn't read the whole file, something is wrong. Free the
        // memory and fail.
        alloc->free(data, alloc->ctx);
        return NULL;
    }

    return data;
}

// write to file
__attribute__((unused)) static bool write_file(const char* path,
                                               const char* data,
                                               size_t      size)
{
    FILE* file = fopen(path, "wb");
    if (!file)
    {
        return false;
    }

    size_t bytes_written = fwrite(data, 1, size, file);
    fclose(file);

    return bytes_written == size;
}

// Helper to check if a file exists and is not empty
__attribute__((unused)) static bool file_exists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        return false;
    }
    fseek(file, 0, SEEK_END);
    const bool is_not_empty = ftell(file) > 0;
    fclose(file);
    return is_not_empty;
}

#endif  // PRELUDE_H
