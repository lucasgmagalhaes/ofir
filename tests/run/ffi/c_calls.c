// The C side of c_calls.of: each function checks what it received from
// Ofir and calls back into the functions Ofir exports.
#include <stdbool.h>
#include <stdint.h>

int8_t c_i8(int8_t v) { return (int8_t)(v - 1); }
uint8_t c_u8(uint8_t v) { return (uint8_t)(v + 1); }
int16_t c_i16(int16_t v) { return (int16_t)(v - 1); }
uint16_t c_u16(uint16_t v) { return (uint16_t)(v + 1); }
int32_t c_i32(int32_t v) { return v - 1; }
uint32_t c_u32(uint32_t v) { return v + 1; }
int64_t c_i64(int64_t v) { return v - 1; }
uint64_t c_u64(uint64_t v) { return v + 1; }
float c_f32(float v) { return v * 2.0f; }
double c_f64(double v) { return v / 2.0; }
bool c_not(bool v) { return !v; }

// Twelve arguments, ints and floats mixed: some go on the stack.
double c_many(int8_t a, double b, uint16_t c, float d, int32_t e, double f, uint64_t g,
              float h, int64_t i, double j, bool k, uint8_t l) {
    return a + b + c + d + e + f + (double)g + h + (double)i + j + (k ? 1000 : 0) + l;
}

// Writes through the pointers it gets and returns the sum.
int64_t c_fill(int32_t *out, int64_t len) {
    int64_t sum = 0;
    for (int64_t i = 0; i < len; i++) {
        out[i] = (int32_t)(i * i);
        sum += out[i];
    }
    return sum;
}

// Exported by Ofir.
int8_t of_i8(int8_t v);
uint16_t of_u16(uint16_t v);
int32_t of_i32(int32_t v);
uint64_t of_u64(uint64_t v);
float of_f32(float v);
double of_f64(double v);
bool of_odd(int64_t v);
double of_many(int8_t a, double b, uint16_t c, float d, int32_t e, double f, uint64_t g,
               float h, int64_t i, double j, bool k, uint8_t l);

// Calls each export with edge values and counts the wrong answers.
int32_t c_call_exports(void) {
    int32_t wrong = 0;
    wrong += of_i8(-128) != 127;
    wrong += of_u16(65535) != 0;
    wrong += of_i32(INT32_MIN) != INT32_MAX;
    wrong += of_u64(UINT64_MAX) != 0;
    wrong += of_f32(1.5f) != 3.0f;
    wrong += of_f64(-0.25) != 0.25;
    wrong += of_odd(7) != true;
    wrong += of_odd(-4) != false;
    double m = of_many(-1, 0.5, 2, 0.25f, -3, 4.0, 5, 0.125f, -6, 7.0, true, 8);
    wrong += m != c_many(-1, 0.5, 2, 0.25f, -3, 4.0, 5, 0.125f, -6, 7.0, true, 8);
    return wrong;
}
