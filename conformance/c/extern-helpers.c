int add_values(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int sum6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

void side_effect(int value) {
    (void)value;  // Suppress unused parameter warning
}
