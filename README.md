# N-Queens OpenMP Code Explanation

## 1. What this program does

This C program solves the **N-Queens problem** using two modes:

1. **Sequential mode** — solves the problem using one thread.
2. **Parallel OpenMP mode** — splits the work into many independent partial board states and solves them in parallel using OpenMP.

The N-Queens problem asks:

> How many ways can we place `N` queens on an `N x N` chessboard so that no two queens attack each other?

Queens attack each other if they are in the same:

- row
- column
- diagonal

The program does **not print the boards themselves**. It counts the number of valid solutions.

---

## 2. How to compile the program

Because the code uses OpenMP, compile it with the `-fopenmp` flag:

```bash
gcc -O3 -fopenmp Pasted\ code.c -o nqueens
```

Recommended version:

```bash
gcc -O3 -march=native -fopenmp Pasted\ code.c -o nqueens
```

Explanation:

- `gcc` — C compiler.
- `-O3` — enables strong compiler optimizations.
- `-march=native` — optimizes for your current CPU.
- `-fopenmp` — enables OpenMP support.
- `Pasted\ code.c` — input source file.
- `-o nqueens` — output executable name.

---

## 3. How to run the program

The program accepts three modes:

```bash
./nqueens seq N [frontier_depth]
./nqueens par N [frontier_depth]
./nqueens both N [frontier_depth]
```

Examples:

```bash
./nqueens seq 16 6
./nqueens par 18 7
./nqueens both 17 6
```

Meaning:

- `seq` — run only the sequential solver.
- `par` — run only the parallel OpenMP solver.
- `both` — run sequential first, then parallel, and compare the results.
- `N` — board size. For example, `17` means a `17 x 17` board.
- `frontier_depth` — how many rows are pre-generated before the actual solving starts.

Example:

```bash
./nqueens both 17 6
```

This means:

- solve the `17 x 17` N-Queens problem
- use frontier depth `6`
- run both sequential and parallel versions
- compare solution count and execution time

---

## 4. Important idea: bitmask representation

The code uses **bitmasks** instead of a normal 2D chessboard array.

This is much faster.

A board row is represented using bits inside a 64-bit integer.

For example, if `N = 8`, then this mask:

```c
0000000000000000000000000000000000000000000000000000000011111111
```

represents the 8 valid columns of the board.

In the code, this is created using:

```c
u64 mask = (1ULL << n) - 1ULL;
```

For `N = 8`:

```c
1ULL << 8 = 100000000
(1ULL << 8) - 1 = 11111111
```

So only the first `N` bits are allowed.

---

## 5. Type alias

```c
typedef uint64_t u64;
```

This creates a shorter name for `uint64_t`.

Instead of writing:

```c
uint64_t columns;
```

we can write:

```c
u64 columns;
```

The code uses `u64` because the solver stores board information in 64-bit integers.

This is also why the program only allows:

```c
N < 64
```

because each column is represented by one bit, and a `uint64_t` has 64 bits.

---

## 6. The `State` struct

```c
typedef struct {
    int row;
    u64 columns;
    u64 diag1;
    u64 diag2;
    u64 multiplier;
} State;
```

A `State` represents a partially solved board.

It stores:

| Field | Meaning |
|---|---|
| `row` | The next row that needs to be solved |
| `columns` | Columns already occupied by queens |
| `diag1` | One diagonal direction already attacked |
| `diag2` | The other diagonal direction already attacked |
| `multiplier` | Used for symmetry optimization |

Instead of storing queen positions in an array, the program stores only which columns and diagonals are blocked.

This makes checking valid moves extremely fast.

---

## 7. The `StateArray` struct

```c
typedef struct {
    State* data;
    size_t size;
    size_t capacity;
} StateArray;
```

This is a dynamic array of `State` objects.

It is used to store the **frontier states**.

A frontier state is a partial board generated before the main solving starts.

The fields mean:

| Field | Meaning |
|---|---|
| `data` | Pointer to the allocated array of states |
| `size` | Number of states currently stored |
| `capacity` | Maximum number of states currently allocated |

The array starts with capacity `4096`. If more space is needed, it doubles its capacity using `realloc`.

---

## 8. Progress bar function

```c
static void print_progress(const char* label, size_t current, size_t total)
```

This function prints a progress bar like:

```text
Sequential [####################--------------------]  50.00% (500/1000)
```

Important parts:

```c
const int width = 40;
```

The progress bar has 40 characters.

```c
double ratio = (double) current / (double) total;
```

This calculates how much work is completed.

```c
int filled = (int)(ratio * width);
```

This calculates how many `#` characters should be printed.

```c
printf("\r%s [", label);
```

The `\r` moves the cursor back to the start of the same line. This allows the program to update the progress bar instead of printing a new line every time.

```c
fflush(stdout);
```

This forces the terminal to immediately show the progress bar.

---

## 9. Initializing and freeing the dynamic array

### `init_state_array`

```c
static void init_state_array(StateArray* array)
```

This function prepares the dynamic array.

It sets:

```c
array->size = 0;
array->capacity = 4096;
```

Then it allocates memory:

```c
array->data = (State*) malloc(array->capacity * sizeof(State));
```

If allocation fails, the program prints an error and exits.

### `free_state_array`

```c
static void free_state_array(StateArray* array)
```

This function frees the memory used by the array.

It also resets the fields to safe values:

```c
array->data = NULL;
array->size = 0;
array->capacity = 0;
```

This prevents accidentally using old memory after it has been freed.

---

## 10. Adding a state

```c
static void add_state(StateArray* array, State state)
```

This function adds one `State` into the dynamic array.

If the array is full:

```c
if (array->size >= array->capacity)
```

then the capacity is doubled:

```c
array->capacity *= 2;
```

and memory is resized using:

```c
realloc
```

Finally, the state is stored:

```c
array->data[array->size++] = state;
```

The `size++` means:

1. store the state at the current index
2. then increase the size by 1

---

## 11. Core recursive solver

```c
static inline u64 solve_recursive(
    int n,
    int row,
    u64 columns,
    u64 diag1,
    u64 diag2,
    u64 mask
)
```

This is the main backtracking function.

It tries to place queens row by row.

### Base case

```c
if (row == n) {
    return 1;
}
```

If `row == n`, it means queens were successfully placed on all rows.

So the function returns `1` valid solution.

### Finding available positions

```c
u64 available = mask & ~(columns | diag1 | diag2);
```

This line calculates which columns are safe in the current row.

Explanation:

- `columns` contains columns already occupied.
- `diag1` contains attacked diagonals in one direction.
- `diag2` contains attacked diagonals in the other direction.
- `columns | diag1 | diag2` combines all blocked positions.
- `~(...)` flips the bits, so blocked positions become available positions.
- `mask & ...` keeps only the valid first `N` columns.

### Taking the lowest available bit

```c
u64 bit = available & -available;
```

This extracts the lowest set bit from `available`.

That bit represents one possible queen position.

Example:

```text
available = 00101000
bit       = 00001000
```

So the solver tries one queen position at a time.

### Removing that bit

```c
available -= bit;
```

This removes the selected position from the available choices.

### Recursive call

```c
count += solve_recursive(
    n,
    row + 1,
    columns | bit,
    (diag1 | bit) << 1,
    (diag2 | bit) >> 1,
    mask
);
```

This places a queen and moves to the next row.

The blocked columns and diagonals are updated:

- `columns | bit` marks the column as occupied.
- `(diag1 | bit) << 1` shifts one diagonal direction for the next row.
- `(diag2 | bit) >> 1` shifts the other diagonal direction for the next row.

This is the reason the bitmask solution is very fast: checking and updating attacks is done with simple CPU bit operations.

---

## 12. Frontier generation

```c
static void generate_frontier(...)
```

This function creates partial board states up to a certain depth.

The `frontier_depth` decides how many rows are solved before creating independent tasks.

For example:

```bash
./nqueens par 18 7
```

means:

- board size is `18`
- generate partial states until row `7`
- then solve each partial state independently

This is very useful for parallelism because each frontier state can be solved separately by a different thread.

### Stop condition

```c
if (row == n || row == frontier_depth)
```

The function stops generating deeper states if:

- the board is already solved, or
- the selected frontier depth has been reached

Then it stores the current partial board as a `State`.

---

## 13. Symmetry optimization in `build_frontier`

```c
static void build_frontier(int n, int frontier_depth, StateArray* states)
```

This function starts the frontier generation.

It uses a symmetry optimization.

In the N-Queens problem, solutions are symmetric across the vertical center of the board.

So instead of placing the first queen in every column of the first row, the code only places it in the left half:

```c
int half = n / 2;

for (int col = 0; col < half; col++) {
    ...
    multiplier = 2ULL;
}
```

Each solution found from the left half also has a mirrored solution on the right half.

That is why the multiplier is `2`.

For odd `N`, there is also a middle column:

```c
if (n % 2 == 1) {
    int middle = half;
    ...
    multiplier = 1ULL;
}
```

The middle column does not have a different mirrored partner, so its multiplier is `1`.

This reduces the search space almost by half.

---

## 14. Sequential solver with progress

```c
static u64 solve_sequential_with_progress(int n, int frontier_depth, size_t* number_of_states)
```

This function solves the problem using one thread.

Steps:

1. Handle the special case `N = 1`.
2. Build the bitmask.
3. Create the frontier states.
4. Loop through the states one by one.
5. Solve each state using `solve_recursive`.
6. Multiply the result by the symmetry multiplier.
7. Update the progress bar.
8. Free memory.
9. Return the total number of solutions.

Important part:

```c
for (size_t i = 0; i < states.size; i++) {
```

This is a normal sequential loop.

Only one state is processed at a time.

---

## 15. Parallel OpenMP solver with progress

```c
static u64 solve_parallel_with_progress(int n, int frontier_depth, size_t* number_of_states)
```

This function is similar to the sequential solver, but the loop over frontier states is parallelized.

The key OpenMP line is:

```c
#pragma omp parallel for schedule(dynamic, 1) reduction(+:total)
```

This means:

- `parallel for` — split the loop across multiple threads.
- `schedule(dynamic, 1)` — give one frontier state at a time to available threads.
- `reduction(+:total)` — safely combine each thread's local result into the final `total`.

### Why dynamic scheduling is used

Not all frontier states take the same amount of time.

Some partial boards lead to many possible solutions and require a lot of recursive search.
Other partial boards fail quickly.

Because the work is uneven, dynamic scheduling helps balance the load between threads.

If one thread finishes early, it gets another state.

### Why reduction is used

Each thread calculates solution counts.

Without `reduction`, multiple threads could update `total` at the same time and cause a race condition.

This line:

```c
reduction(+:total)
```

makes OpenMP safely add all partial totals together.

---

## 16. Progress tracking in parallel mode

Parallel progress is more complicated because multiple threads finish states at the same time.

The code uses:

```c
#pragma omp atomic capture
current_completed = ++completed;
```

This safely increments `completed` by one.

The `atomic capture` ensures that no two threads corrupt the counter.

Then the progress bar printing is protected by:

```c
#pragma omp critical
```

This ensures only one thread prints at a time.

Without this, multiple threads could print over each other and mess up the terminal output.

---

## 17. Default frontier depth

```c
static int default_frontier_depth(int n)
```

This function chooses a default frontier depth based on `N`:

```c
if (n <= 12) {
    return 4;
} else if (n <= 15) {
    return 5;
} else if (n <= 17) {
    return 6;
} else {
    return 7;
}
```

The idea is:

- smaller boards do not need many frontier states
- larger boards need more frontier states to give enough work to all threads

A higher frontier depth creates more tasks, which can improve parallel load balancing.

But if the frontier depth is too high, the program spends too much time generating states and storing them.

So it is a balance.

---

## 18. Command-line parsing

The program expects at least two arguments after the executable name:

```c
if (argc < 3)
```

Required:

```text
mode N
```

Optional:

```text
frontier_depth
```

The code uses:

```c
parse_long
```

to safely parse numbers.

This function checks if the input is valid.

For example, this is valid:

```bash
./nqueens par 18 7
```

This is invalid:

```bash
./nqueens par abc 7
```

because `abc` is not a number.

---

## 19. Program modes

### Sequential mode

```bash
./nqueens seq 17 6
```

Runs only the sequential version.

Output includes:

- mode
- board size
- frontier depth
- number of frontier states
- number of solutions
- execution time

### Parallel mode

```bash
./nqueens par 18 7
```

Runs only the OpenMP parallel version.

Output also includes:

- number of OpenMP threads

### Both mode

```bash
./nqueens both 17 6
```

Runs sequential first, then parallel.

It prints:

- sequential solution count
- parallel solution count
- sequential time
- parallel time
- speedup
- efficiency

It also checks correctness:

```c
if (seq_solutions != par_solutions)
```

If the results do not match, the program prints a warning and exits with failure.

---

## 20. Speedup and efficiency

In `both` mode, the program calculates:

```c
double speedup = seq_time / par_time;
double efficiency = speedup / omp_get_max_threads();
```

### Speedup

Speedup tells how many times faster the parallel version is.

Example:

```text
Sequential time: 80 seconds
Parallel time: 10 seconds
Speedup: 8x
```

This means the parallel version is 8 times faster.

### Efficiency

Efficiency tells how well the threads are being used.

Example:

```text
Speedup: 6.4x
Threads: 8
Efficiency: 0.8
```

This means the program achieved 80% efficiency compared to perfect scaling.

Perfect efficiency is hard to reach because of:

- thread scheduling overhead
- uneven work between states
- progress bar synchronization
- memory and CPU limits

---

## 21. What `N` and `frontier_depth` mean

In a command like:

```bash
./nqueens par 18 7
```

`18` is `N`.

That means the board is:

```text
18 x 18
```

`7` is the `frontier_depth`.

That means the program first generates partial boards up to row 7. Then each partial board is solved independently.

For sequential mode, `frontier_depth` is not needed for parallelism, but this program still uses it because both sequential and parallel versions are designed around the same frontier system. This makes the comparison fairer.

---

## 22. Why this is optimized

This code is optimized because it uses:

1. **Bitmasks** instead of a 2D board.
2. **Recursive backtracking** with fast bit operations.
3. **Symmetry optimization** to cut almost half the search space.
4. **Frontier generation** to create independent chunks of work.
5. **OpenMP parallel for** to solve chunks in parallel.
6. **Dynamic scheduling** to balance uneven work.
7. **Reduction** to safely combine results.
8. **Progress bar** to show that the program is still running.

---

## 23. Possible downside of the progress bar

The progress bar is useful for visibility, but it can slightly slow down the parallel version.

This is because printing requires synchronization:

```c
#pragma omp critical
```

Only one thread can enter the critical section at a time.

However, the code avoids printing after every single state by using:

```c
progress_step = states.size / 100;
```

So it usually prints around 100 updates instead of thousands or millions.

That is a good compromise.

---

## 24. Example output

Example command:

```bash
OMP_NUM_THREADS=8 ./nqueens both 17 6
```

Possible output format:

```text
Running sequential solver...
Sequential [########################################] 100.00% (.../...)

Running parallel OpenMP solver...
Parallel   [########################################] 100.00% (.../...)

Mode: Sequential + Parallel comparison
N: 17
Frontier depth: 6
Frontier states: ...
Threads: 8

Sequential solutions: ...
Parallel solutions:   ...

Sequential time: ... seconds
Parallel time:   ... seconds
Speedup:         ...x
Efficiency:      ...
```

The exact time depends on your CPU, number of threads, compiler, and system load.

---

## 25. Overall summary

This program is a high-performance N-Queens solver written in C.

The main algorithm is recursive backtracking, but it is optimized using bitmasks. Instead of checking the board manually, the program stores occupied columns and diagonals as bits. This makes each move check extremely fast.

The parallel version works by first generating many partial board states called frontier states. These states are independent, so OpenMP can distribute them across multiple threads. Each thread solves different states using the same recursive solver. The final answer is combined safely using OpenMP reduction.

The program also includes symmetry optimization. It only explores half of the first row and doubles the result, while handling the middle column separately when `N` is odd.

In short, the code is efficient because it combines:

- bit-level representation
- backtracking
- symmetry reduction
- task-style frontier splitting
- OpenMP parallelism
- dynamic scheduling

This makes it suitable for comparing sequential and parallel performance in a parallel programming course.
