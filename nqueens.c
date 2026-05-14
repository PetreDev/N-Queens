#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <omp.h>

typedef uint64_t u64;

typedef struct {
    int row;
    u64 columns;
    u64 diag1;
    u64 diag2;
    u64 multiplier;
} State;

typedef struct {
    State* data;
    size_t size;
    size_t capacity;
} StateArray;

static void print_progress(const char* label, size_t current, size_t total) {
    const int width = 40;

    double ratio = 0.0;

    if (total > 0) {
        ratio = (double) current / (double) total;
    }

    int filled = (int)(ratio * width);

    printf("\r%s [", label);

    for (int i = 0; i < width; i++) {
        if (i < filled) {
            printf("#");
        } else {
            printf("-");
        }
    }

    printf("] %6.2f%% (%zu/%zu)", ratio * 100.0, current, total);
    fflush(stdout);

    if (current == total) {
        printf("\n");
    }
}

static void init_state_array(StateArray* array) {
    array->size = 0;
    array->capacity = 4096;
    array->data = (State*) malloc(array->capacity * sizeof(State));

    if (array->data == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
}

static void free_state_array(StateArray* array) {
    free(array->data);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
}

static void add_state(StateArray* array, State state) {
    if (array->size >= array->capacity) {
        array->capacity *= 2;

        State* new_data = (State*) realloc(array->data, array->capacity * sizeof(State));

        if (new_data == NULL) {
            fprintf(stderr, "Error: memory reallocation failed.\n");
            free(array->data);
            exit(EXIT_FAILURE);
        }

        array->data = new_data;
    }

    array->data[array->size++] = state;
}

static inline u64 solve_recursive(
    int n,
    int row,
    u64 columns,
    u64 diag1,
    u64 diag2,
    u64 mask
) {
    if (row == n) {
        return 1;
    }

    u64 count = 0;
    u64 available = mask & ~(columns | diag1 | diag2);

    while (available) {
        u64 bit = available & -available;
        available -= bit;

        count += solve_recursive(
            n,
            row + 1,
            columns | bit,
            (diag1 | bit) << 1,
            (diag2 | bit) >> 1,
            mask
        );
    }

    return count;
}

static void generate_frontier(
    int n,
    int row,
    u64 columns,
    u64 diag1,
    u64 diag2,
    u64 mask,
    int frontier_depth,
    u64 multiplier,
    StateArray* states
) {
    if (row == n || row == frontier_depth) {
        State state;
        state.row = row;
        state.columns = columns;
        state.diag1 = diag1;
        state.diag2 = diag2;
        state.multiplier = multiplier;

        add_state(states, state);
        return;
    }

    u64 available = mask & ~(columns | diag1 | diag2);

    while (available) {
        u64 bit = available & -available;
        available -= bit;

        generate_frontier(
            n,
            row + 1,
            columns | bit,
            (diag1 | bit) << 1,
            (diag2 | bit) >> 1,
            mask,
            frontier_depth,
            multiplier,
            states
        );
    }
}

static void build_frontier(int n, int frontier_depth, StateArray* states) {
    u64 mask = (1ULL << n) - 1ULL;
    int half = n / 2;

    for (int col = 0; col < half; col++) {
        u64 bit = 1ULL << col;

        generate_frontier(
            n,
            1,
            bit,
            bit << 1,
            bit >> 1,
            mask,
            frontier_depth,
            2ULL,
            states
        );
    }

    if (n % 2 == 1) {
        int middle = half;
        u64 bit = 1ULL << middle;

        generate_frontier(
            n,
            1,
            bit,
            bit << 1,
            bit >> 1,
            mask,
            frontier_depth,
            1ULL,
            states
        );
    }
}

static u64 solve_sequential_with_progress(int n, int frontier_depth, size_t* number_of_states) {
    if (n == 1) {
        if (number_of_states != NULL) {
            *number_of_states = 1;
        }

        print_progress("Sequential", 1, 1);
        return 1;
    }

    u64 mask = (1ULL << n) - 1ULL;

    StateArray states;
    init_state_array(&states);

    build_frontier(n, frontier_depth, &states);

    if (number_of_states != NULL) {
        *number_of_states = states.size;
    }

    u64 total = 0;

    size_t progress_step = states.size / 100;

    if (progress_step == 0) {
        progress_step = 1;
    }

    print_progress("Sequential", 0, states.size);

    for (size_t i = 0; i < states.size; i++) {
        State state = states.data[i];

        u64 local_count = solve_recursive(
            n,
            state.row,
            state.columns,
            state.diag1,
            state.diag2,
            mask
        );

        total += state.multiplier * local_count;

        size_t completed = i + 1;

        if (completed % progress_step == 0 || completed == states.size) {
            print_progress("Sequential", completed, states.size);
        }
    }

    free_state_array(&states);

    return total;
}

static u64 solve_parallel_with_progress(int n, int frontier_depth, size_t* number_of_states) {
    if (n == 1) {
        if (number_of_states != NULL) {
            *number_of_states = 1;
        }

        print_progress("Parallel  ", 1, 1);
        return 1;
    }

    u64 mask = (1ULL << n) - 1ULL;

    StateArray states;
    init_state_array(&states);

    build_frontier(n, frontier_depth, &states);

    if (number_of_states != NULL) {
        *number_of_states = states.size;
    }

    u64 total = 0;
    size_t completed = 0;

    size_t progress_step = states.size / 100;

    if (progress_step == 0) {
        progress_step = 1;
    }

    print_progress("Parallel  ", 0, states.size);

    #pragma omp parallel for schedule(dynamic, 1) reduction(+:total)
    for (size_t i = 0; i < states.size; i++) {
        State state = states.data[i];

        u64 local_count = solve_recursive(
            n,
            state.row,
            state.columns,
            state.diag1,
            state.diag2,
            mask
        );

        total += state.multiplier * local_count;

        size_t current_completed;

        #pragma omp atomic capture
        current_completed = ++completed;

        #pragma omp critical
        {
            if (current_completed % progress_step == 0 || current_completed == states.size) {
                print_progress("Parallel  ", current_completed, states.size);
            }
        }
    }

    free_state_array(&states);

    return total;
}

static int default_frontier_depth(int n) {
    if (n <= 12) {
        return 4;
    } else if (n <= 15) {
        return 5;
    } else if (n <= 17) {
        return 6;
    } else {
        return 7;
    }
}

static void print_usage(const char* program_name) {
    printf("Usage:\n");
    printf("  %s seq N [frontier_depth]\n", program_name);
    printf("  %s par N [frontier_depth]\n", program_name);
    printf("  %s both N [frontier_depth]\n", program_name);
    printf("\nExamples:\n");
    printf("  %s seq 16 6\n", program_name);
    printf("  %s par 18 7\n", program_name);
    printf("  %s both 17 6\n", program_name);
}

static int parse_long(const char* str, const char* name, long* out) {
    char* end;
    errno = 0;
    long val = strtol(str, &end, 10);

    if (errno != 0 || end == str || *end != '\0') {
        fprintf(stderr, "Error: invalid value for %s: '%s'\n", name, str);
        return 0;
    }

    *out = val;
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* mode = argv[1];

    long n_long;
    if (!parse_long(argv[2], "N", &n_long)) {
        return EXIT_FAILURE;
    }

    int n = (int) n_long;

    if (n <= 0 || n >= 64) {
        fprintf(stderr, "Error: N must be between 1 and 63.\n");
        return EXIT_FAILURE;
    }

    int frontier_depth = default_frontier_depth(n);

    if (argc >= 4) {
        long fd_long;
        if (!parse_long(argv[3], "frontier_depth", &fd_long)) {
            return EXIT_FAILURE;
        }
        frontier_depth = (int) fd_long;
    }

    if (frontier_depth < 1 || frontier_depth > n) {
        fprintf(stderr, "Error: frontier_depth must be between 1 and N.\n");
        return EXIT_FAILURE;
    }

    if (strcmp(mode, "seq") == 0) {
        size_t states_count = 0;

        printf("Running sequential solver...\n");

        double start = omp_get_wtime();

        u64 solutions = solve_sequential_with_progress(n, frontier_depth, &states_count);

        double end = omp_get_wtime();

        printf("\nMode: Sequential\n");
        printf("N: %d\n", n);
        printf("Frontier depth: %d\n", frontier_depth);
        printf("Frontier states: %zu\n", states_count);
        printf("Solutions: %" PRIu64 "\n", solutions);
        printf("Time: %.6f seconds\n", end - start);
    }
    else if (strcmp(mode, "par") == 0) {
        size_t states_count = 0;

        printf("Running parallel OpenMP solver...\n");

        double start = omp_get_wtime();

        u64 solutions = solve_parallel_with_progress(n, frontier_depth, &states_count);

        double end = omp_get_wtime();

        printf("\nMode: Parallel OpenMP\n");
        printf("N: %d\n", n);
        printf("Frontier depth: %d\n", frontier_depth);
        printf("Frontier states: %zu\n", states_count);
        printf("Threads: %d\n", omp_get_max_threads());
        printf("Solutions: %" PRIu64 "\n", solutions);
        printf("Time: %.6f seconds\n", end - start);
    }
    else if (strcmp(mode, "both") == 0) {
        size_t seq_states_count = 0;
        size_t par_states_count = 0;

        printf("Running sequential solver...\n");

        double seq_start = omp_get_wtime();

        u64 seq_solutions = solve_sequential_with_progress(n, frontier_depth, &seq_states_count);

        double seq_end = omp_get_wtime();

        printf("\nRunning parallel OpenMP solver...\n");

        double par_start = omp_get_wtime();

        u64 par_solutions = solve_parallel_with_progress(n, frontier_depth, &par_states_count);

        double par_end = omp_get_wtime();

        double seq_time = seq_end - seq_start;
        double par_time = par_end - par_start;
        double speedup = seq_time / par_time;
        double efficiency = speedup / omp_get_max_threads();

        printf("\nMode: Sequential + Parallel comparison\n");
        printf("N: %d\n", n);
        printf("Frontier depth: %d\n", frontier_depth);
        printf("Frontier states: %zu\n", par_states_count);
        printf("Threads: %d\n", omp_get_max_threads());
        printf("\n");

        printf("Sequential solutions: %" PRIu64 "\n", seq_solutions);
        printf("Parallel solutions:   %" PRIu64 "\n", par_solutions);
        printf("\n");

        printf("Sequential time: %.6f seconds\n", seq_time);
        printf("Parallel time:   %.6f seconds\n", par_time);
        printf("Speedup:         %.4fx\n", speedup);
        printf("Efficiency:      %.4f\n", efficiency);

        if (seq_solutions != par_solutions) {
            fprintf(stderr, "\nWarning: sequential and parallel results do not match!\n");
            return EXIT_FAILURE;
        }
    }
    else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}