#include <iostream>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <iomanip>
#include <string>
#include <random>
#include <chrono>
#include <algorithm>
#include <stdexcept>

// ==================== Banker's Algorithm Class ====================
class BankersAlgorithm {
private:
    int numProcesses;
    int numResources;
    std::vector<int> available;
    std::vector<std::vector<int>> maximum;
    std::vector<std::vector<int>> allocation;
    std::vector<std::vector<int>> need;
    std::mutex bankMutex;
    std::condition_variable cv;
    std::vector<bool> finished;
    std::mutex printMutex;

public:
    BankersAlgorithm(int processes, int resources,
                     const std::vector<int>& avail,
                     const std::vector<std::vector<int>>& max,
                     const std::vector<std::vector<int>>& alloc)
        : numProcesses(processes),
          numResources(resources),
          available(avail),
          maximum(max),
          allocation(alloc),
          finished(processes, false)
    {
        // Validate input data
        validateInitialState();

        // Calculate need matrix: Need = Maximum - Allocation
        need.resize(numProcesses, std::vector<int>(numResources));
        for (int i = 0; i < numProcesses; ++i) {
            for (int j = 0; j < numResources; ++j) {
                need[i][j] = maximum[i][j] - allocation[i][j];
            }
        }
    }

    // Validate initial configuration
    void validateInitialState() {
        for (int i = 0; i < numProcesses; ++i) {
            for (int j = 0; j < numResources; ++j) {
                if (allocation[i][j] > maximum[i][j]) {
                    throw std::invalid_argument(
                        "Ошибка: Allocation[" + std::to_string(i) + "][" + 
                        std::to_string(j) + "] > Maximum");
                }
                if (allocation[i][j] < 0 || maximum[i][j] < 0) {
                    throw std::invalid_argument("Ошибка: Отрицательные значения недопустимы");
                }
            }
        }

        // Check if sum of allocations doesn't exceed total resources
        for (int j = 0; j < numResources; ++j) {
            int sum = available[j];
            for (int i = 0; i < numProcesses; ++i) {
                sum += allocation[i][j];
            }
            if (sum < 0) {
                throw std::invalid_argument(
                    "Ошибка: Некорректные начальные данные для ресурса " + std::to_string(j));
            }
        }
    }

    // Check if request can be granted (safety algorithm)
    bool isSafe() {
        std::vector<int> work = available;
        std::vector<bool> finish(numProcesses, false);
        std::vector<int> safeSequence;
        int count = 0;

        while (count < numProcesses) {
            bool found = false;
            for (int i = 0; i < numProcesses; ++i) {
                // ИСПРАВЛЕНИЕ: Пропускаем завершённые процессы
                if (!finish[i] && !finished[i]) {
                    // Check if process i can be allocated
                    bool canAllocate = true;
                    for (int j = 0; j < numResources; ++j) {
                        if (need[i][j] > work[j]) {
                            canAllocate = false;
                            break;
                        }
                    }

                    if (canAllocate) {
                        // Process i can finish, release its resources
                        for (int j = 0; j < numResources; ++j) {
                            work[j] += allocation[i][j];
                        }
                        finish[i] = true;
                        safeSequence.push_back(i);
                        found = true;
                        count++;
                    }
                }
            }

            if (!found) {
                // Check if all remaining processes are finished
                bool allFinished = true;
                for (int i = 0; i < numProcesses; ++i) {
                    if (!finish[i] && !finished[i]) {
                        allFinished = false;
                        break;
                    }
                }
                if (allFinished) {
                    return true;
                }
                return false;
            }
        }
        return true;
    }

    bool findSafeSequence(std::vector<int>& sequence) {
        std::vector<int> work = available;
        std::vector<bool> finish(numProcesses, false);
        sequence.clear();
        int count = 0;

        while (count < numProcesses) {
            bool found = false;
            for (int i = 0; i < numProcesses; ++i) {
                if (!finish[i] && !finished[i]) {
                    bool canAllocate = true;
                    for (int j = 0; j < numResources; ++j) {
                        if (need[i][j] > work[j]) {
                            canAllocate = false;
                            break;
                        }
                    }

                    if (canAllocate) {
                        for (int j = 0; j < numResources; ++j) {
                            work[j] += allocation[i][j];
                        }
                        finish[i] = true;
                        sequence.push_back(i);
                        found = true;
                        count++;
                    }
                }
            }

            if (!found) {
                // Check if all remaining are finished
                bool allFinished = true;
                for (int i = 0; i < numProcesses; ++i) {
                    if (!finish[i] && !finished[i]) {
                        allFinished = false;
                        break;
                    }
                }
                if (allFinished) {
                    return true;
                }
                return false;
            }
        }
        return true;
    }

    // Request resources with retry mechanism
    bool requestResources(int processId, const std::vector<int>& request, 
                         int maxRetries = 5, int timeoutMs = 1000) {
        // Don't allow requests from finished processes
        if (finished[processId]) {
            std::lock_guard<std::mutex> printLock(printMutex);
            std::cout << "[P" << processId << "] ✗ ОТКЛОНЕНО: процесс уже завершён\n";
            return false;
        }

        int retries = 0;

        while (retries < maxRetries) {
            std::unique_lock<std::mutex> lock(bankMutex);

            {
                std::lock_guard<std::mutex> printLock(printMutex);
                std::cout << "\n[P" << processId << "] Запрос ресурсов";
                if (retries > 0) {
                    std::cout << " (попытка " << (retries + 1) << "/" << maxRetries << ")";
                }
                std::cout << ": ";
                printVector(request);
            }

            // Check if request exceeds need
            bool exceedsNeed = false;
            for (int j = 0; j < numResources; ++j) {
                if (request[j] > need[processId][j]) {
                    exceedsNeed = true;
                    break;
                }
            }

            if (exceedsNeed) {
                std::lock_guard<std::mutex> printLock(printMutex);
                std::cout << "[P" << processId << "] ✗ ОТКЛОНЕНО: запрос превышает заявленную потребность\n";
                return false;
            }

            // Check if resources are available
            bool resourcesAvailable = true;
            for (int j = 0; j < numResources; ++j) {
                if (request[j] > available[j]) {
                    resourcesAvailable = false;
                    break;
                }
            }

            if (!resourcesAvailable) {
                std::lock_guard<std::mutex> printLock(printMutex);
                std::cout << "[P" << processId << "] ⏳ ОЖИДАНИЕ: недостаточно доступных ресурсов\n";

                // Wait with timeout
                bool signaled = cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                    [&]() {
                        for (int k = 0; k < numResources; ++k) {
                            if (request[k] > available[k]) return false;
                        }
                        return true;
                    });

                if (!signaled) {
                    retries++;
                    continue;
                }

                // Recheck availability after wake up
                resourcesAvailable = true;
                for (int j = 0; j < numResources; ++j) {
                    if (request[j] > available[j]) {
                        resourcesAvailable = false;
                        break;
                    }
                }

                if (!resourcesAvailable) {
                    retries++;
                    continue;
                }
            }

            // Temporarily allocate resources
            for (int j = 0; j < numResources; ++j) {
                available[j] -= request[j];
                allocation[processId][j] += request[j];
                need[processId][j] -= request[j];
            }

            // Check if state is safe
            if (isSafe()) {
                std::lock_guard<std::mutex> printLock(printMutex);
                std::cout << "[P" << processId << "] ✓ ВЫДЕЛЕНО: состояние безопасно\n";
                return true;
            } else {
                // Rollback allocation
                for (int j = 0; j < numResources; ++j) {
                    available[j] += request[j];
                    allocation[processId][j] -= request[j];
                    need[processId][j] += request[j];
                }

                {
                    std::lock_guard<std::mutex> printLock(printMutex);
                    std::cout << "[P" << processId << "] ✗ ОТКЛОНЕНО: приведет к небезопасному состоянию\n";
                }

                // Wait before retry
                cv.wait_for(lock, std::chrono::milliseconds(timeoutMs));
                retries++;
            }
        }

        // Max retries exceeded
        std::lock_guard<std::mutex> printLock(printMutex);
        std::cout << "[P" << processId << "] ✗ ОТКАЗ: превышено максимальное число попыток\n";
        return false;
    }

    // ИСПРАВЛЕНИЕ: Всегда завершаем процесс при освобождении ресурсов
    void releaseResources(int processId) {
        std::unique_lock<std::mutex> lock(bankMutex);

        if (finished[processId]) {
            std::lock_guard<std::mutex> printLock(printMutex);
            std::cout << "[P" << processId << "] ⚠ Процесс уже завершён\n";
            return;
        }

        {
            std::lock_guard<std::mutex> printLock(printMutex);
            std::cout << "\n[P" << processId << "] Освобождение ресурсов: ";
            printVector(allocation[processId]);
        }

        // Release all allocated resources
        for (int j = 0; j < numResources; ++j) {
            available[j] += allocation[processId][j];
            allocation[processId][j] = 0;
            need[processId][j] = 0; // Process finished - no more needs
        }

        finished[processId] = true;

        {
            std::lock_guard<std::mutex> printLock(printMutex);
            std::cout << "[P" << processId << "] ✓ Ресурсы освобождены, процесс завершён\n";
        }

        // Notify waiting processes
        cv.notify_all();
    }

    // Get number of active (not finished) processes
    int getActiveProcessCount() const {
        int count = 0;
        for (int i = 0; i < numProcesses; ++i) {
            if (!finished[i]) {
                count++;
            }
        }
        return count;
    }

    // Print current state with better formatting for finished processes
    void printState() {
        std::lock_guard<std::mutex> lock(printMutex);
        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "         ТЕКУЩЕЕ СОСТОЯНИЕ СИСТЕМЫ\n";
        std::cout << std::string(60, '-') << "\n";

        std::cout << "\nДоступные ресурсы (Available):\n   ";
        for (int j = 0; j < numResources; ++j) {
            std::cout << "R" << j << "=" << available[j] << " ";
        }
        std::cout << "\n";

        // Count active processes
        int activeCount = getActiveProcessCount();
        std::cout << "\nАктивных процессов: " << activeCount << " из " << numProcesses << "\n";

        if (activeCount > 0) {
            std::cout << "\nМатрица максимальных потребностей (Maximum):\n";
            printMatrixWithStatus(maximum, "Maximum");

            std::cout << "\nМатрица выделенных ресурсов (Allocation):\n";
            printMatrixWithStatus(allocation, "Allocation");

            std::cout << "\nМатрица оставшихся потребностей (Need):\n";
            printMatrixWithStatus(need, "Need");
        } else {
            std::cout << "\n✓ Все процессы завершены, все ресурсы освобождены\n";
        }

        std::cout << std::string(60, '-') << "\n";
    }

private:
    void printVector(const std::vector<int>& v) {
        std::cout << "[";
        for (size_t i = 0; i < v.size(); ++i) {
            std::cout << v[i];
            if (i < v.size() - 1) std::cout << ", ";
        }
        std::cout << "]\n";
    }

    void printMatrixWithStatus(const std::vector<std::vector<int>>& matrix, 
                               const std::string& matrixName) {
        std::cout << "     ";
        for (int j = 0; j < numResources; ++j) {
            std::cout << std::setw(4) << "R" << j;
        }
        std::cout << "    Status\n";

        for (int i = 0; i < numProcesses; ++i) {
            // Only show active processes
            if (!finished[i]) {
                std::cout << "   P" << i << ": ";
                for (int j = 0; j < numResources; ++j) {
                    std::cout << std::setw(4) << matrix[i][j];
                }
                std::cout << "    [Active]\n";
            }
        }
    }

    void printMatrix(const std::vector<std::vector<int>>& matrix, const std::string& rowPrefix) {
        std::cout << "     ";
        for (int j = 0; j < numResources; ++j) {
            std::cout << std::setw(4) << "R" << j;
        }
        std::cout << "\n";

        for (int i = 0; i < numProcesses; ++i) {
            std::cout << "   " << rowPrefix << i << ": ";
            for (int j = 0; j < numResources; ++j) {
                std::cout << std::setw(4) << matrix[i][j];
            }
            std::cout << "\n";
        }
    }
};

// ==================== Process Simulation ====================
void simulateProcess(BankersAlgorithm& bank, int processId,
                    const std::vector<std::vector<int>>& requests) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sleep_dist(100, 500);

    for (const auto& request : requests) {
        // Random delay before request
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));

        // Try to request resources (with retries)
        if (bank.requestResources(processId, request)) {
            // Simulate work with resources
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));
        }
    }

    // Release all resources when done
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));
    bank.releaseResources(processId);
}

// ==================== Main ====================
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Лабораторная работа №4 - Задание 3, Вариант 1              ║\n";
    std::cout << "║     Алгоритм банкира (Banker's Algorithm) - УЛУЧШЕННАЯ ВЕРСИЯ  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";

    // ==================== Example 1: Manual Configuration ====================
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "         ПРИМЕР 1: СТАТИЧЕСКИЙ АНАЛИЗ\n";
    std::cout << std::string(70, '=') << "\n";

    int numProcesses = 5;
    int numResources = 3;
    std::vector<int> available = {3, 3, 2};

    std::vector<std::vector<int>> maximum = {
        {7, 5, 3}, // P0
        {3, 2, 2}, // P1
        {9, 0, 2}, // P2
        {2, 2, 2}, // P3
        {4, 3, 3}  // P4
    };

    std::vector<std::vector<int>> allocation = {
        {0, 1, 0}, // P0
        {2, 0, 0}, // P1
        {3, 0, 2}, // P2
        {2, 1, 1}, // P3
        {0, 0, 2}  // P4
    };

    try {
        BankersAlgorithm bank1(numProcesses, numResources, available, maximum, allocation);

        std::cout << "\nНачальная конфигурация системы:\n";
        std::cout << "   - Процессов: " << numProcesses << "\n";
        std::cout << "   - Типов ресурсов: " << numResources << " (A, B, C)\n";
        std::cout << "   - Всего ресурсов: [10, 5, 7]\n";
        bank1.printState();

        // Check safety and find safe sequence
        std::vector<int> safeSequence;
        if (bank1.findSafeSequence(safeSequence)) {
            std::cout << "\n✓ СИСТЕМА В БЕЗОПАСНОМ СОСТОЯНИИ\n";
            std::cout << "Безопасная последовательность: < ";
            for (size_t i = 0; i < safeSequence.size(); ++i) {
                std::cout << "P" << safeSequence[i];
                if (i < safeSequence.size() - 1) std::cout << " → ";
            }
            std::cout << " >\n";
        } else {
            std::cout << "\n✗ СИСТЕМА В НЕБЕЗОПАСНОМ СОСТОЯНИИ\n";
            std::cout << "Возможна взаимоблокировка (deadlock)!\n";
        }

        // Test resource requests
        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "Тест запросов ресурсов:\n";
        std::cout << std::string(60, '-') << "\n";

        std::cout << "\nТест 1: P1 запрашивает [1, 0, 2]";
        bank1.requestResources(1, {1, 0, 2});

        BankersAlgorithm bank2(numProcesses, numResources, available, maximum, allocation);
        std::cout << "\nТест 2: P4 запрашивает [3, 3, 0]";
        bank2.requestResources(4, {3, 3, 0});

        std::cout << "\nТест 3: P0 запрашивает [0, 2, 0]";
        bank2.requestResources(0, {0, 2, 0});

    } catch (const std::exception& e) {
        std::cerr << "Ошибка инициализации: " << e.what() << "\n";
        return 1;
    }

    // ==================== Example 2: Multithreaded Simulation ====================
    std::cout << "\n\n" << std::string(70, '=') << "\n";
    std::cout << "         ПРИМЕР 2: МНОГОПОТОЧНАЯ СИМУЛЯЦИЯ\n";
    std::cout << std::string(70, '=') << "\n";

    int numProc2 = 3;
    int numRes2 = 2;
    std::vector<int> avail2 = {5, 5};

    std::vector<std::vector<int>> max2 = {
        {4, 3}, // P0
        {3, 4}, // P1
        {4, 4}  // P2
    };

    std::vector<std::vector<int>> alloc2 = {
        {1, 1}, // P0
        {1, 1}, // P1
        {1, 1}  // P2
    };

    try {
        BankersAlgorithm bank3(numProc2, numRes2, avail2, max2, alloc2);

        std::cout << "\nКонфигурация для многопоточной симуляции:\n";
        std::cout << "   - Процессов: " << numProc2 << "\n";
        std::cout << "   - Типов ресурсов: " << numRes2 << "\n";
        bank3.printState();

        std::vector<std::vector<std::vector<int>>> processRequests = {
            {{1, 0}, {1, 1}}, // P0's requests
            {{0, 1}, {1, 1}}, // P1's requests
            {{1, 1}, {1, 0}}  // P2's requests
        };

        std::cout << "\nЗапуск процессов...\n";
        std::cout << std::string(60, '-') << "\n";

        std::vector<std::thread> threads;
        for (int i = 0; i < numProc2; ++i) {
            threads.emplace_back(simulateProcess, std::ref(bank3), i,
                               std::cref(processRequests[i]));
        }

        for (auto& t : threads) {
            t.join();
        }

        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "Симуляция завершена.\n";
        bank3.printState();

    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
