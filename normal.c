#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#ifndef __EMSCRIPTEN__
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#define printf(...) ((void)0)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int id;
    int left;
    int right;
    char state[16];
    int held[2];
    int held_count;
    int waiting_for;
    int eat_ticks;
    int think_ticks;
} Philosopher;

typedef struct {
    char value[32];
} StrategySelect;

typedef struct {
    int value;
} CountInput;

static CountInput count_input = { 5 };
static StrategySelect strategy_select = { "unsafe" };
static Philosopher philosophers[10];
static int philosopher_count = 0;
static int chopsticks[10];
static int chopstick_count = 0;
static int step_number = 0;
static bool running = false;
static bool deadlocked = false;

static Philosopher create_philosopher(int id, int count) {
    Philosopher p;
    p.id = id;
    p.left = (id + count - 1) % count;
    p.right = id;
    strncpy(p.state, "Thinking", sizeof(p.state) - 1);
    p.state[sizeof(p.state) - 1] = '\0';
    p.held_count = 0;
    p.held[0] = -1;
    p.held[1] = -1;
    p.waiting_for = -1;
    p.eat_ticks = 0;
    p.think_ticks = 0;
    return p;
}

static void build_table(void) {
    int count = count_input.value;
    if (count < 2) count = 2;
    if (count > 10) count = 10;
    count_input.value = count;
    philosopher_count = count;
    chopstick_count = count;

    for (int id = 0; id < count; id++) {
        philosophers[id] = create_philosopher(id, count);
        chopsticks[id] = -1;
    }
}

static void acquisition_order(Philosopher* philosopher, int* out_hands) {
    out_hands[0] = philosopher->left;
    out_hands[1] = philosopher->right;

    if (strcmp(strategy_select.value, "ordering") == 0) {
        if (out_hands[0] > out_hands[1]) {
            int temp = out_hands[0];
            out_hands[0] = out_hands[1];
            out_hands[1] = temp;
        }
        return;
    }

    if (strcmp(strategy_select.value, "asymmetric") == 0 && philosopher->id % 2 == 1) {
        int temp = out_hands[0];
        out_hands[0] = out_hands[1];
        out_hands[1] = temp;
    }
}

static void begin_eating(Philosopher* philosopher) {
    strncpy(philosopher->state, "Eating", sizeof(philosopher->state) - 1);
    philosopher->state[sizeof(philosopher->state) - 1] = '\0';
    philosopher->waiting_for = -1;
    philosopher->eat_ticks = 3;
    printf("P%d acquired both chopsticks and entered the critical section.\n", philosopher->id + 1);
}

static void release_resources(Philosopher* philosopher) {
    for (int i = 0; i < philosopher->held_count; i++) {
        int index = philosopher->held[i];
        if (chopsticks[index] == philosopher->id) {
            chopsticks[index] = -1;
        }
    }
    philosopher->held_count = 0;
}

static void release_after_eating(Philosopher* philosopher) {
    release_resources(philosopher);
    strncpy(philosopher->state, "Thinking", sizeof(philosopher->state) - 1);
    philosopher->state[sizeof(philosopher->state) - 1] = '\0';
    philosopher->waiting_for = -1;
    philosopher->think_ticks = 2;
    printf("P%d finished eating and released both chopsticks.\n", philosopher->id + 1);
}

static void request_resources(Philosopher* philosopher) {
    if (strcmp(strategy_select.value, "waiter") == 0) {
        bool someone_else_holds = false;
        for (int i = 0; i < philosopher_count; i++) {
            if (philosophers[i].id != philosopher->id && philosophers[i].held_count > 0) {
                someone_else_holds = true;
                break;
            }
        }

        if (someone_else_holds) {
            strncpy(philosopher->state, "Waiting", sizeof(philosopher->state) - 1);
            philosopher->state[sizeof(philosopher->state) - 1] = '\0';
            philosopher->waiting_for = -1;
            return;
        }

        if (chopsticks[philosopher->left] == -1 && chopsticks[philosopher->right] == -1) {
            chopsticks[philosopher->left] = philosopher->id;
            chopsticks[philosopher->right] = philosopher->id;
            philosopher->held[0] = philosopher->left;
            philosopher->held[1] = philosopher->right;
            philosopher->held_count = 2;
            begin_eating(philosopher);
        } else {
            strncpy(philosopher->state, "Waiting", sizeof(philosopher->state) - 1);
            philosopher->state[sizeof(philosopher->state) - 1] = '\0';
        }
        return;
    }

    int order[2];
    acquisition_order(philosopher, order);
    int next_stick = philosopher->held_count == 0
        ? order[0]
        : (philosopher->left == philosopher->held[0] ? philosopher->right : philosopher->left);

    if (strcmp(strategy_select.value, "limit") == 0 && philosopher->held_count == 0) {
        int holders = 0;
        for (int i = 0; i < philosopher_count; i++) {
            if (philosophers[i].held_count > 0) holders++;
        }
        if (holders >= philosopher_count - 1) {
            strncpy(philosopher->state, "Waiting", sizeof(philosopher->state) - 1);
            philosopher->state[sizeof(philosopher->state) - 1] = '\0';
            philosopher->waiting_for = -1;
            return;
        }
    }

    if (chopsticks[next_stick] != -1 && chopsticks[next_stick] != philosopher->id) {
        strncpy(philosopher->state, "Waiting", sizeof(philosopher->state) - 1);
        philosopher->state[sizeof(philosopher->state) - 1] = '\0';
        philosopher->waiting_for = next_stick;
        return;
    }

    chopsticks[next_stick] = philosopher->id;
    bool already_held = false;
    for (int i = 0; i < philosopher->held_count; i++) {
        if (philosopher->held[i] == next_stick) {
            already_held = true;
            break;
        }
    }
    if (!already_held) philosopher->held[philosopher->held_count++] = next_stick;

    if (philosopher->held_count == 2) {
        begin_eating(philosopher);
    } else {
        strncpy(philosopher->state, "Waiting", sizeof(philosopher->state) - 1);
        philosopher->state[sizeof(philosopher->state) - 1] = '\0';
        philosopher->waiting_for = philosopher->left == next_stick ? philosopher->right : philosopher->left;
        printf("P%d holds C%d and waits for C%d.\n", philosopher->id + 1, next_stick + 1, philosopher->waiting_for + 1);
    }
}

static bool detect_deadlock(void) {
    if (philosopher_count < 2) return false;

    for (int i = 0; i < philosopher_count; i++) {
        if (strcmp(philosophers[i].state, "Waiting") != 0 || philosophers[i].held_count == 0 || philosophers[i].waiting_for == -1) {
            return false;
        }
    }

    int current = 0;
    bool visited[10] = { false };
    int visited_count = 0;

    for (int step = 0; step < philosopher_count; step++) {
        if (visited[current]) return false;
        visited[current] = true;
        visited_count++;
        int owner = chopsticks[philosophers[current].waiting_for];
        if (owner == -1 || owner == current) return false;
        current = owner;
    }

    return current == 0 && visited_count == philosopher_count;
}

static void render(void) {
    int eating_count = 0;
    int waiting_count = 0;
    int free_count = 0;

    for (int i = 0; i < philosopher_count; i++) {
        if (strcmp(philosophers[i].state, "Eating") == 0) eating_count++;
        if (strcmp(philosophers[i].state, "Waiting") == 0) waiting_count++;
    }

    for (int i = 0; i < chopstick_count; i++) {
        if (chopsticks[i] == -1) free_count++;
    }

    printf("Step %d | eating=%d waiting=%d free=%d deadlock=%s\n",
           step_number, eating_count, waiting_count, free_count, deadlocked ? "yes" : "no");

    for (int i = 0; i < philosopher_count; i++) {
        printf("P%d: %s | held=%d/%d | waiting_for=%d\n",
               philosophers[i].id + 1,
               philosophers[i].state,
               philosophers[i].held_count,
               2,
               philosophers[i].waiting_for);
    }
}

static void advance_simulation(void) {
    step_number += 1;

    for (int i = 0; i < philosopher_count; i++) {
        Philosopher* philosopher = &philosophers[i];
        if (strcmp(philosopher->state, "Eating") == 0) {
            philosopher->eat_ticks -= 1;
            if (philosopher->eat_ticks <= 0) {
                release_after_eating(philosopher);
            }
        } else if (strcmp(philosopher->state, "Thinking") == 0) {
            philosopher->think_ticks -= 1;
            if (philosopher->think_ticks <= 0) {
                strncpy(philosopher->state, "Hungry", sizeof(philosopher->state) - 1);
                philosopher->state[sizeof(philosopher->state) - 1] = '\0';
            }
        }
    }

    for (int i = 0; i < philosopher_count; i++) {
        Philosopher* philosopher = &philosophers[i];
        if (strcmp(philosopher->state, "Hungry") == 0 || strcmp(philosopher->state, "Waiting") == 0) {
            request_resources(philosopher);
        }
    }

    deadlocked = detect_deadlock();
    render();

    if (deadlocked) {
        printf("Deadlock detected.\n");
    }
}

__attribute__((visibility("default")))
void normal_init(int count, int strategy) {
    static const char* strategy_keys[] = { "unsafe", "limit", "ordering", "waiter", "asymmetric" };
    count_input.value = count;
    if (strategy < 0 || strategy >= 5) strategy = 0;
    strncpy(strategy_select.value, strategy_keys[strategy], sizeof(strategy_select.value) - 1);
    strategy_select.value[sizeof(strategy_select.value) - 1] = '\0';
    build_table();
    step_number = 0;
    running = false;
    deadlocked = false;
}

__attribute__((visibility("default")))
void normal_start(void) {
    if (deadlocked) return;
    if (step_number == 0) {
        for (int i = 0; i < philosopher_count; i++) {
            strncpy(philosophers[i].state, "Hungry", sizeof(philosophers[i].state) - 1);
            philosophers[i].state[sizeof(philosophers[i].state) - 1] = '\0';
        }
    }
    running = true;
}

__attribute__((visibility("default")))
void normal_pause(void) {
    running = false;
}

__attribute__((visibility("default")))
void normal_step(void) {
    if (!running || deadlocked) return;
    advance_simulation();
}

__attribute__((visibility("default")))
int normal_count(void) { return philosopher_count; }

__attribute__((visibility("default")))
int normal_running(void) { return running; }

__attribute__((visibility("default")))
int normal_deadlocked(void) { return deadlocked; }

__attribute__((visibility("default")))
int normal_step_number(void) { return step_number; }

__attribute__((visibility("default")))
int normal_state(int id) {
    if (id < 0 || id >= philosopher_count) return 0;
    if (strcmp(philosophers[id].state, "Hungry") == 0) return 1;
    if (strcmp(philosophers[id].state, "Waiting") == 0) return 2;
    if (strcmp(philosophers[id].state, "Eating") == 0) return 3;
    return 0;
}

__attribute__((visibility("default")))
int normal_held(int id, int slot) {
    if (id < 0 || id >= philosopher_count || slot < 0 || slot >= 2) return -1;
    return philosophers[id].held[slot];
}

__attribute__((visibility("default")))
int normal_waiting_for(int id) {
    if (id < 0 || id >= philosopher_count) return -1;
    return philosophers[id].waiting_for;
}

__attribute__((visibility("default")))
int normal_chopstick_owner(int id) {
    if (id < 0 || id >= chopstick_count) return -1;
    return chopsticks[id];
}

#ifndef __EMSCRIPTEN__
typedef enum {
    SYNC_SEMAPHORES,
    SYNC_MUTEXES,
    SYNC_MONITOR
} SyncMethod;

typedef struct {
    int id;
    SyncMethod method;
    int meals;
} Worker;

typedef enum {
    MONITOR_THINKING,
    MONITOR_HUNGRY,
    MONITOR_EATING
} MonitorState;

static int demo_count = 5;
static sem_t* demo_forks[10];
static sem_t* demo_room;
static pthread_mutex_t demo_fork_mutexes[10];
static pthread_mutex_t demo_output_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t monitor_conditions[10];
static MonitorState monitor_states[10];

static sem_t* create_named_semaphore(const char* role, int id, unsigned int initial_value) {
    char name[64];
    snprintf(name, sizeof(name), "/dp_%s_%d_%d", role, (int)getpid(), id);
    sem_t* semaphore = sem_open(name, O_CREAT | O_EXCL, 0600, initial_value);
    if (semaphore != SEM_FAILED) sem_unlink(name);
    return semaphore;
}

static void monitor_test(int id) {
    int left_neighbor = (id + demo_count - 1) % demo_count;
    int right_neighbor = (id + 1) % demo_count;
    if (monitor_states[id] == MONITOR_HUNGRY &&
        monitor_states[left_neighbor] != MONITOR_EATING &&
        monitor_states[right_neighbor] != MONITOR_EATING) {
        monitor_states[id] = MONITOR_EATING;
        pthread_cond_signal(&monitor_conditions[id]);
    }
}

static void monitor_wait_for_forks(int id) {
    pthread_mutex_lock(&monitor_mutex);
    monitor_states[id] = MONITOR_HUNGRY;
    monitor_test(id);
    while (monitor_states[id] != MONITOR_EATING) {
        pthread_cond_wait(&monitor_conditions[id], &monitor_mutex);
    }
    pthread_mutex_unlock(&monitor_mutex);
}

static void monitor_release_forks(int id) {
    pthread_mutex_lock(&monitor_mutex);
    monitor_states[id] = MONITOR_THINKING;
    monitor_test((id + demo_count - 1) % demo_count);
    monitor_test((id + 1) % demo_count);
    pthread_mutex_unlock(&monitor_mutex);
}

static void log_meal(int id, int meal, int left, int right) {
    pthread_mutex_lock(&demo_output_mutex);
    printf("P%d completed meal %d using C%d and C%d.\n", id + 1, meal, left + 1, right + 1);
    pthread_mutex_unlock(&demo_output_mutex);
}

static void* run_philosopher(void* data) {
    Worker* worker = (Worker*)data;
    int left = worker->id;
    int right = (worker->id + 1) % demo_count;

    for (int meal = 1; meal <= worker->meals; meal++) {
        if (worker->method == SYNC_SEMAPHORES) {
            sem_wait(demo_room);
            sem_wait(demo_forks[left]);
            sem_wait(demo_forks[right]);
            log_meal(worker->id, meal, left, right);
            sem_post(demo_forks[right]);
            sem_post(demo_forks[left]);
            sem_post(demo_room);
        } else if (worker->method == SYNC_MUTEXES) {
            int first = left < right ? left : right;
            int second = left < right ? right : left;
            pthread_mutex_lock(&demo_fork_mutexes[first]);
            pthread_mutex_lock(&demo_fork_mutexes[second]);
            log_meal(worker->id, meal, left, right);
            pthread_mutex_unlock(&demo_fork_mutexes[second]);
            pthread_mutex_unlock(&demo_fork_mutexes[first]);
        } else {
            monitor_wait_for_forks(worker->id);
            log_meal(worker->id, meal, left, right);
            monitor_release_forks(worker->id);
        }
    }
    return NULL;
}

static int run_sync_demo(SyncMethod method, const char* label) {
    pthread_t threads[10];
    Worker workers[10];
    int initialized_forks = 0;
    int initialized_conditions = 0;

    if (method == SYNC_SEMAPHORES) {
        demo_room = create_named_semaphore("room", 0, (unsigned int)(demo_count - 1));
        if (demo_room == SEM_FAILED) return 1;
        for (; initialized_forks < demo_count; initialized_forks++) {
            demo_forks[initialized_forks] = create_named_semaphore("fork", initialized_forks, 1);
            if (demo_forks[initialized_forks] == SEM_FAILED) return 1;
        }
    } else if (method == SYNC_MUTEXES) {
        for (; initialized_forks < demo_count; initialized_forks++) {
            if (pthread_mutex_init(&demo_fork_mutexes[initialized_forks], NULL) != 0) return 1;
        }
    } else {
        for (int i = 0; i < demo_count; i++) monitor_states[i] = MONITOR_THINKING;
        for (; initialized_conditions < demo_count; initialized_conditions++) {
            if (pthread_cond_init(&monitor_conditions[initialized_conditions], NULL) != 0) return 1;
        }
    }

    printf("\n%s synchronization: %d philosophers, 3 meals each\n", label, demo_count);
    int created = 0;
    for (; created < demo_count; created++) {
        workers[created] = (Worker){ created, method, 3 };
        if (pthread_create(&threads[created], NULL, run_philosopher, &workers[created]) != 0) break;
    }
    for (int i = 0; i < created; i++) pthread_join(threads[i], NULL);

    if (method == SYNC_SEMAPHORES) {
        for (int i = 0; i < initialized_forks; i++) sem_close(demo_forks[i]);
        sem_close(demo_room);
    } else if (method == SYNC_MUTEXES) {
        for (int i = 0; i < initialized_forks; i++) pthread_mutex_destroy(&demo_fork_mutexes[i]);
    } else {
        for (int i = 0; i < initialized_conditions; i++) pthread_cond_destroy(&monitor_conditions[i]);
    }
    return created == demo_count ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "semaphores") == 0) {
        return run_sync_demo(SYNC_SEMAPHORES, "Semaphore wait/post");
    }
    if (argc > 1 && strcmp(argv[1], "mutex") == 0) {
        return run_sync_demo(SYNC_MUTEXES, "Ordered mutex locks");
    }
    if (argc > 1 && strcmp(argv[1], "monitor") == 0) {
        return run_sync_demo(SYNC_MONITOR, "Monitor condition wait/signal");
    }
    if (argc > 1 && strcmp(argv[1], "all") != 0) {
        fprintf(stderr, "Usage: %s [semaphores|mutex|monitor|all]\n", argv[0]);
        return 2;
    }
    if (run_sync_demo(SYNC_SEMAPHORES, "Semaphore wait/post") != 0) return 1;
    if (run_sync_demo(SYNC_MUTEXES, "Ordered mutex locks") != 0) return 1;
    if (run_sync_demo(SYNC_MONITOR, "Monitor condition wait/signal") != 0) return 1;
    return 0;
}
#endif
