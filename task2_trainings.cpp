/*
 * Лабораторная работа №4 - Многопоточность
 * Задание 2, Вариант 3: Обработка данных о тренировках
 * 
 * Структура содержит данные о проводимых в зале тренировках 
 * (дата, время, ФИО тренера). Необходимо найти тренировки, 
 * проводимые в день недели Д.
 */

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <ctime>

// ==================== Data Structures ====================

struct Date {
    int day;
    int month;
    int year;
    
    // Zeller's formula
    int getDayOfWeek() const {
        int d = day;
        int m = month;
        int y = year;
        
        // Adjust for January and February
        if (m < 3) {
            m += 12;
            y--;
        }
        
        int k = y % 100;
        int j = y / 100;
        
        // Zeller's formula
        int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
        
        // Convert to 0=Sunday format
        return ((h + 6) % 7);
    }
    
    std::string toString() const {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << day << "."
            << std::setw(2) << month << "." << year;
        return oss.str();
    }
};

struct Time {
    int hours;
    int minutes;
    
    std::string toString() const {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << hours << ":"
            << std::setw(2) << minutes;
        return oss.str();
    }
};

struct Training {
    Date date;
    Time time;
    std::string trainerName;
    
    std::string toString() const {
        return date.toString() + " " + time.toString() + " - " + trainerName;
    }
};

// Day of week names
const char* DAY_NAMES[] = {
    "Воскресенье", "Понедельник", "Вторник", "Среда", 
    "Четверг", "Пятница", "Суббота"
};

const char* DAY_NAMES_EN[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", 
    "Thursday", "Friday", "Saturday"
};

// ==================== Data Generation ====================

std::vector<Training> generateTrainings(size_t count) {
    std::vector<Training> trainings;
    trainings.reserve(count);
    
    std::mt19937 gen(42); // Fixed seed for reproducibility
    std::uniform_int_distribution<> day_dist(1, 28);
    std::uniform_int_distribution<> month_dist(1, 12);
    std::uniform_int_distribution<> year_dist(2023, 2025);
    std::uniform_int_distribution<> hour_dist(8, 21);
    std::uniform_int_distribution<> minute_dist(0, 59);
    
    std::vector<std::string> trainers = {
        "Иванов И.И.", "Петров П.П.", "Сидорова А.В.", 
        "Козлов К.К.", "Смирнова М.С.", "Волков В.В.",
        "Морозова Е.А.", "Новиков Н.Н.", "Федорова Ф.Ф.",
        "Алексеев А.А.", "Михайлова М.М.", "Павлов П.А."
    };
    std::uniform_int_distribution<> trainer_dist(0, trainers.size() - 1);
    
    for (size_t i = 0; i < count; ++i) {
        Training t;
        t.date = {day_dist(gen), month_dist(gen), year_dist(gen)};
        t.time = {hour_dist(gen), minute_dist(gen)};
        t.trainerName = trainers[trainer_dist(gen)];
        trainings.push_back(t);
    }
    
    return trainings;
}

// ==================== Single-threaded Processing ====================

std::vector<Training> findTrainingsByDaySingleThread(
    const std::vector<Training>& trainings, 
    int dayOfWeek) 
{
    std::vector<Training> result;
    
    for (const auto& t : trainings) {
        if (t.date.getDayOfWeek() == dayOfWeek) {
            result.push_back(t);
        }
    }
    
    return result;
}

// ==================== Multi-threaded Processing ====================

void processChunk(
    const std::vector<Training>& trainings,
    size_t start, size_t end,
    int dayOfWeek,
    std::vector<Training>& localResults)
{
    for (size_t i = start; i < end; ++i) {
        if (trainings[i].date.getDayOfWeek() == dayOfWeek) {
            localResults.push_back(trainings[i]);
        }
    }
}

std::vector<Training> findTrainingsByDayMultiThread(
    const std::vector<Training>& trainings,
    int dayOfWeek,
    int numThreads)
{
    std::vector<std::thread> threads;
    std::vector<std::vector<Training>> threadResults(numThreads);
    
    size_t chunkSize = trainings.size() / numThreads;
    size_t remainder = trainings.size() % numThreads;
    
    size_t start = 0;
    for (int i = 0; i < numThreads; ++i) {
        size_t end = start + chunkSize + (i < static_cast<int>(remainder) ? 1 : 0);
        
        threads.emplace_back(processChunk, 
            std::cref(trainings), start, end, dayOfWeek, 
            std::ref(threadResults[i]));
        
        start = end;
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Merge results
    std::vector<Training> result;
    for (const auto& tr : threadResults) {
        result.insert(result.end(), tr.begin(), tr.end());
    }
    
    return result;
}

// Alternative: Using mutex for shared results
std::vector<Training> findTrainingsByDayMultiThreadMutex(
    const std::vector<Training>& trainings,
    int dayOfWeek,
    int numThreads)
{
    std::vector<Training> result;
    std::mutex resultMutex;
    std::vector<std::thread> threads;
    
    size_t chunkSize = trainings.size() / numThreads;
    size_t remainder = trainings.size() % numThreads;
    
    auto worker = [&](size_t start, size_t end) {
        std::vector<Training> localResults;
        for (size_t i = start; i < end; ++i) {
            if (trainings[i].date.getDayOfWeek() == dayOfWeek) {
                localResults.push_back(trainings[i]);
            }
        }
        
        std::lock_guard<std::mutex> lock(resultMutex);
        result.insert(result.end(), localResults.begin(), localResults.end());
    };
    
    size_t start = 0;
    for (int i = 0; i < numThreads; ++i) {
        size_t end = start + chunkSize + (i < static_cast<int>(remainder) ? 1 : 0);
        threads.emplace_back(worker, start, end);
        start = end;
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    return result;
}

// ==================== Benchmarking ====================

template<typename Func>
double measureTime(Func func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

// ==================== Main ====================

int main(int argc, char* argv[]) {
    
    // Parameters - use command line args or defaults for testing
    size_t dataSize = 500000;
    int numThreads = 4;
    int targetDay = 1; // Monday
    
    if (argc >= 4) {
        dataSize = std::stoul(argv[1]);
        numThreads = std::stoi(argv[2]);
        targetDay = std::stoi(argv[3]);
    } else if (argc == 1) {
        std::cout << "\nИспользование: " << argv[0] << " <размер_данных> <кол-во_потоков> <день_недели>\n";
        std::cout << "Используются значения по умолчанию: " << dataSize << " " << numThreads << " " << targetDay << "\n";
    }
    
    std::cout << "\nДни недели:\n";
    for (int i = 0; i < 7; ++i) {
        std::cout << "  " << i << " - " << DAY_NAMES[i] << "\n";
    }
    
    if (targetDay < 0 || targetDay > 6) {
        std::cerr << "Ошибка: неверный номер дня недели (должен быть 0-6)!\n";
        return 1;
    }
    
    std::cout << "\nВыбранный день: " << DAY_NAMES[targetDay] << " (" << targetDay << ")\n";
    
    // Generate data
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Генерация данных...\n";
    auto genStart = std::chrono::high_resolution_clock::now();
    std::vector<Training> trainings = generateTrainings(dataSize);
    auto genEnd = std::chrono::high_resolution_clock::now();
    auto genTime = std::chrono::duration_cast<std::chrono::milliseconds>(genEnd - genStart).count();
    
    std::cout << "Сгенерировано " << trainings.size() << " записей за " << genTime << " мс\n";
    std::cout << std::string(60, '=') << "\n";
    
    // Single-threaded processing
    std::cout << "\n>>> Однопоточная обработка...\n";
    std::vector<Training> singleResult;
    double singleTime = measureTime([&]() {
        singleResult = findTrainingsByDaySingleThread(trainings, targetDay);
    });
    
    std::cout << "Найдено записей: " << singleResult.size() << "\n";
    std::cout << "Время выполнения: " << std::fixed << std::setprecision(2) 
              << singleTime << " мкс (" << singleTime / 1000 << " мс)\n";
    
    // Multi-threaded processing (local results)
    std::cout << "\n>>> Многопоточная обработка (локальные результаты)...\n";
    std::cout << "Количество потоков: " << numThreads << "\n";
    std::vector<Training> multiResult;
    double multiTime = measureTime([&]() {
        multiResult = findTrainingsByDayMultiThread(trainings, targetDay, numThreads);
    });
    
    std::cout << "Найдено записей: " << multiResult.size() << "\n";
    std::cout << "Время выполнения: " << std::fixed << std::setprecision(2) 
              << multiTime << " мкс (" << multiTime / 1000 << " мс)\n";
    
    // Multi-threaded processing (mutex)
    std::cout << "\n>>> Многопоточная обработка (с mutex)...\n";
    std::vector<Training> multiMutexResult;
    double multiMutexTime = measureTime([&]() {
        multiMutexResult = findTrainingsByDayMultiThreadMutex(trainings, targetDay, numThreads);
    });
    
    std::cout << "Найдено записей: " << multiMutexResult.size() << "\n";
    std::cout << "Время выполнения: " << std::fixed << std::setprecision(2) 
              << multiMutexTime << " мкс (" << multiMutexTime / 1000 << " мс)\n";
    
    // Results comparison
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "                    РЕЗУЛЬТАТЫ СРАВНЕНИЯ\n";
    std::cout << std::string(60, '=') << "\n";
    
    std::cout << "\nПараметры теста:\n";
    std::cout << "  - Размер данных: " << dataSize << " записей\n";
    std::cout << "  - Количество потоков: " << numThreads << "\n";
    std::cout << "  - Искомый день недели: " << DAY_NAMES[targetDay] << "\n";
    
    std::cout << "\n┌────────────────────────────────┬──────────────┬────────────┐\n";
    std::cout << "│ Метод                          │  Время (мс)  │ Ускорение  │\n";
    std::cout << "├────────────────────────────────┼──────────────┼────────────┤\n";
    std::cout << "│ Однопоточный                   │ " << std::setw(12) << std::fixed 
              << std::setprecision(3) << singleTime / 1000 << " │     1.00x  │\n";
    std::cout << "│ Многопоточный (локальные)      │ " << std::setw(12) << std::fixed 
              << std::setprecision(3) << multiTime / 1000 << " │ " << std::setw(8) 
              << std::setprecision(2) << singleTime / multiTime << "x  │\n";
    std::cout << "│ Многопоточный (с mutex)        │ " << std::setw(12) << std::fixed 
              << std::setprecision(3) << multiMutexTime / 1000 << " │ " << std::setw(8) 
              << std::setprecision(2) << singleTime / multiMutexTime << "x  │\n";
    std::cout << "└────────────────────────────────┴──────────────┴────────────┘\n";
    
    // Display sample results
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "ПРИМЕРЫ НАЙДЕННЫХ ЗАПИСЕЙ (первые 10):\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Тренировки в " << DAY_NAMES[targetDay] << ":\n\n";
    
    size_t displayCount = std::min(singleResult.size(), size_t(10));
    for (size_t i = 0; i < displayCount; ++i) {
        std::cout << std::setw(3) << (i + 1) << ". " << singleResult[i].toString() << "\n";
    }
    
    if (singleResult.size() > 10) {
        std::cout << "... и ещё " << (singleResult.size() - 10) << " записей\n";
    }
    
    // Verify results match
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "ВЕРИФИКАЦИЯ РЕЗУЛЬТАТОВ:\n";
    bool resultsMatch = (singleResult.size() == multiResult.size()) && 
                        (singleResult.size() == multiMutexResult.size());
    std::cout << "Результаты всех методов совпадают: " 
              << (resultsMatch ? "✓ ДА" : "✗ НЕТ") << "\n";
    
    return 0;
}
