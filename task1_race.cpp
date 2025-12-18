/*
 * Лабораторная работа №4 - Многопоточность
 * Задание 1: Параллельный запуск потоков в формате гонки
 * Сравнительный анализ примитивов синхронизации
 * 
 * Barrier работает в режиме синхронизированного старта (фазовая синхронизация)
 */

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <condition_variable>
#include <semaphore>
#include <barrier>
#include <iomanip>
#include <functional>
#include <sstream>

// ==================== Synchronization Primitives ====================

// 1. Mutex wrapper
class MutexSync {
private:
    std::mutex mtx;
public:
    void init(int) {}  // no-op
    void lock() { mtx.lock(); }
    void unlock() { mtx.unlock(); }
    static const char* name() { return "Mutex"; }
};

// 2. SpinLock implementation
class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void init(int) {}  // no-op
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // Spin
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
    static const char* name() { return "SpinLock"; }
};

// 3. SpinWait implementation (SpinLock with yield)
class SpinWait {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    int spin_count = 0;
    static constexpr int MAX_SPIN = 100;
public:
    void init(int) {}  // no-op
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            if (++spin_count > MAX_SPIN) {
                std::this_thread::yield();
                spin_count = 0;
            }
        }
        spin_count = 0;
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
    static const char* name() { return "SpinWait"; }
};

// 4. Monitor implementation (mutex + condition variable)
class Monitor {
private:
    std::mutex mtx;
    std::condition_variable cv;
    bool locked = false;
public:
    void init(int) {}  // no-op
    void lock() {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this]{ return !locked; });
        locked = true;
    }
    void unlock() {
        std::lock_guard<std::mutex> lk(mtx);
        locked = false;
        cv.notify_one();
    }
    static const char* name() { return "Monitor"; }
};

// 5. Semaphore wrapper (binary semaphore acts like mutex)
class SemaphoreSync {
private:
    std::counting_semaphore<1> sem{1};
public:
    void init(int) {}  // no-op
    void lock() { sem.acquire(); }
    void unlock() { sem.release(); }
    static const char* name() { return "Semaphore"; }
};

// 6. Barrier - синхронизированный старт (фазовая синхронизация)
//    Все потоки ждут друг друга на каждой итерации
class BarrierSync {
private:
    std::barrier<>* bar = nullptr;
    int num_threads = 0;
public:
    ~BarrierSync() { 
        delete bar; 
    }
    
    void init(int threads) {
        num_threads = threads;
        bar = new std::barrier<>(threads);
    }
    
    // Синхронизированный старт - все потоки ждут друг друга
    void lock() { 
        bar->arrive_and_wait(); 
    }
    
    void unlock() { 
        // no-op - барьер не требует разблокировки
    }
    
    static const char* name() { return "Barrier"; }
};


// ==================== Race Simulation ====================

// Global variables for race
std::atomic<int> finish_position{0};
std::atomic<bool> race_started{false};

// Generate random ASCII character (printable range: 33-126)
char getRandomAscii() {
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<> dist(33, 126);
    return static_cast<char>(dist(gen));
}

// Race participant thread function
template<typename SyncPrimitive>
void raceParticipant(int id, int race_distance, SyncPrimitive& sync,
                     std::vector<std::pair<int, char>>& results,
                     std::mutex& results_mutex) {
    // Wait for race start
    while (!race_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    
    char my_char = getRandomAscii();
    
    // Simulate race progress
    for (int i = 0; i < race_distance; ++i) {
        sync.lock();
        // Critical section - simulate work
        volatile int dummy = 0;
        for (int j = 0; j < 100; ++j) dummy += j;
        sync.unlock();
    }
    
    // Record finish position
    int pos = finish_position.fetch_add(1, std::memory_order_relaxed) + 1;
    
    std::lock_guard<std::mutex> lk(results_mutex);
    results.push_back({pos, my_char});
}

// Run race with specific synchronization primitive
template<typename SyncPrimitive>
double runRace(int num_threads, int race_distance) {
    SyncPrimitive sync;
    sync.init(num_threads);  // Initialize with thread count (needed for Barrier)
    
    std::vector<std::thread> threads;
    std::vector<std::pair<int, char>> results;
    std::mutex results_mutex;
    
    // Reset race state
    finish_position.store(0);
    race_started.store(false);
    
    // Create threads
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(raceParticipant<SyncPrimitive>, 
                            i, race_distance, std::ref(sync),
                            std::ref(results), std::ref(results_mutex));
    }
    
    // Start timing and race
    auto start = std::chrono::high_resolution_clock::now();
    race_started.store(true, std::memory_order_release);
    
    // Wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Sort results by position
    std::sort(results.begin(), results.end());
    
    // Print race results
    std::cout << "\n=== Race Results using " << SyncPrimitive::name() << " ===\n";
    std::cout << "Position | Character | Thread\n";
    std::cout << "---------|-----------|---------\n";
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << std::setw(8) << results[i].first << " | "
                  << std::setw(9) << results[i].second << " | "
                  << "Thread " << i << "\n";
    }
    
    return duration.count();
}


// ==================== Benchmark ====================

struct BenchmarkResult {
    std::string primitive_name;
    double avg_time_us;
    double min_time_us;
    double max_time_us;
    int iterations;
};

template<typename SyncPrimitive>
BenchmarkResult benchmark(int num_threads, int race_distance, int iterations) {
    std::vector<double> times;
    times.reserve(iterations);
    
    for (int i = 0; i < iterations; ++i) {
        // Suppress output during benchmark
        std::cout.setstate(std::ios_base::failbit);
        double time = runRace<SyncPrimitive>(num_threads, race_distance);
        std::cout.clear();
        times.push_back(time);
    }
    
    double avg = 0, min_t = times[0], max_t = times[0];
    for (double t : times) {
        avg += t;
        min_t = std::min(min_t, t);
        max_t = std::max(max_t, t);
    }
    avg /= iterations;
    
    return {SyncPrimitive::name(), avg, min_t, max_t, iterations};
}

void printBenchmarkResults(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "                    BENCHMARK RESULTS\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::cout << std::left << std::setw(15) << "Primitive"
              << std::right << std::setw(15) << "Avg (μs)"
              << std::setw(15) << "Min (μs)"
              << std::setw(15) << "Max (μs)"
              << std::setw(12) << "Iterations" << "\n";
    std::cout << std::string(70, '-') << "\n";
    
    for (const auto& r : results) {
        std::cout << std::left << std::setw(15) << r.primitive_name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(15) << r.avg_time_us
                  << std::setw(15) << r.min_time_us
                  << std::setw(15) << r.max_time_us
                  << std::setw(12) << r.iterations << "\n";
    }
    std::cout << std::string(70, '-') << "\n";
    
    // Find fastest
    auto fastest = std::min_element(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.avg_time_us < b.avg_time_us; });
    std::cout << "\n🏆 Fastest: " << fastest->primitive_name 
              << " (" << fastest->avg_time_us << " μs avg)\n";
}

// ==================== Main ====================

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Лабораторная работа №4 - Задание 1: Гонка потоков        ║\n";
    std::cout << "║     Сравнительный анализ примитивов синхронизации            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    const int NUM_THREADS = 8;
    const int RACE_DISTANCE = 1000;
    const int BENCHMARK_ITERATIONS = 10;
    
    std::cout << "\nПараметры:\n";
    std::cout << "  - Количество потоков: " << NUM_THREADS << "\n";
    std::cout << "  - Дистанция гонки: " << RACE_DISTANCE << " итераций\n";
    std::cout << "  - Итерации бенчмарка: " << BENCHMARK_ITERATIONS << "\n";
    
    // Demo runs with output
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "ДЕМОНСТРАЦИЯ РАБОТЫ КАЖДОГО ПРИМИТИВА\n";
    std::cout << std::string(60, '=') << "\n";
    
    runRace<MutexSync>(NUM_THREADS, 100);
    runRace<SpinLock>(NUM_THREADS, 100);
    runRace<SpinWait>(NUM_THREADS, 100);
    runRace<Monitor>(NUM_THREADS, 100);
    runRace<SemaphoreSync>(NUM_THREADS, 100);
    runRace<BarrierSync>(NUM_THREADS, 100);  // Barrier с синхростартом
    
    
    // Benchmark all primitives
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "ЗАПУСК БЕНЧМАРКА...\n";
    std::cout << std::string(60, '=') << "\n";
    
    std::vector<BenchmarkResult> results;
    
    std::cout << "Testing Mutex...\n";
    results.push_back(benchmark<MutexSync>(NUM_THREADS, RACE_DISTANCE, BENCHMARK_ITERATIONS));
    
    std::cout << "Testing SpinLock...\n";
    results.push_back(benchmark<SpinLock>(NUM_THREADS, RACE_DISTANCE, BENCHMARK_ITERATIONS));
    
    std::cout << "Testing SpinWait...\n";
    results.push_back(benchmark<SpinWait>(NUM_THREADS, RACE_DISTANCE, BENCHMARK_ITERATIONS));
    
    std::cout << "Testing Monitor...\n";
    results.push_back(benchmark<Monitor>(NUM_THREADS, RACE_DISTANCE, BENCHMARK_ITERATIONS));
    
    std::cout << "Testing Semaphore...\n";
    results.push_back(benchmark<SemaphoreSync>(NUM_THREADS, RACE_DISTANCE, BENCHMARK_ITERATIONS));

    std::cout << "Testing Barrier (synchronized start)...\n";
    results.push_back(benchmark<BarrierSync>(NUM_THREADS, RACE_DISTANCE, BENCHMARK_ITERATIONS));

    
    printBenchmarkResults(results);
    
    // Analysis
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "АНАЛИЗ РЕЗУЛЬТАТОВ\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << R"(
Mutex:
  + Надежный, поддерживается ОС, эффективен при длительных блокировках
  - Накладные расходы на системные вызовы
  Режим: взаимное исключение (один поток в критической секции)

SpinLock:
  + Минимальные накладные расходы при коротких блокировках
  - Потребляет CPU при ожидании, неэффективен при длительных блокировках
  Режим: взаимное исключение (активное ожидание)

SpinWait:
  + Компромисс между SpinLock и Mutex
  + Уступает CPU после определенного числа итераций
  - Сложнее в настройке
  Режим: взаимное исключение (гибридное ожидание)

Monitor:
  + Позволяет ждать выполнения условия
  + Эффективен для producer-consumer паттернов
  - Дополнительные накладные расходы на condition_variable
  Режим: взаимное исключение + условная синхронизация

Semaphore:
  + Гибкий - может ограничивать доступ N потоков
  + Хорошо подходит для ограничения ресурсов
  - Небольшие накладные расходы по сравнению с mutex
  Режим: счетчик доступа (в данном случае бинарный = mutex)

Barrier:
  + Синхронизирует группу потоков в определенной точке
  + Идеален для параллельных алгоритмов с фазами
  + Все потоки стартуют одновременно (синхронизированный старт)
  - Не для взаимного исключения - все потоки работают параллельно
  Режим: фазовая синхронизация (все ждут всех, потом все стартуют)
)";
    
    return 0;
}
