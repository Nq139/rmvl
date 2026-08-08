/**
 * @file test_log.cpp
 * @author Nq139 (1585062440@qq.com)
 * @brief 日志模块测试
 * @version 1.0
 * @date 2026-08-07
 *
 * @copyright Copyright 2026 (c), Nq139
 *
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include <gtest/gtest.h>

#include "rmvl/log/log.hpp"

namespace rm_test {

namespace {

class RecordingSink final : public rm::Sink {
public:
    explicit RecordingSink(bool result = true) : _result(result) {}

    bool write(const rm::LogRecord &record) override {
        std::lock_guard<std::mutex> lock(_mtx);
        _records.push_back(record);
        return _result;
    }

    std::vector<rm::LogRecord> records() const {
        std::lock_guard<std::mutex> lock(_mtx);
        return _records;
    }

private:
    bool _result;
    mutable std::mutex _mtx;
    std::vector<rm::LogRecord> _records;
};

std::string dateOf(std::chrono::system_clock::time_point time) {
    const auto raw = std::chrono::system_clock::to_time_t(time);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d");
    return stream.str();
}

std::filesystem::path makeTempDirectory(const char *name) {
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string("rmvl-log-") + name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

TEST(LOG_logger, single_sink_success_and_format) {
    rm::Logger logger;
    auto sink = std::make_unique<RecordingSink>();
    auto *sink_ptr = sink.get();
    ASSERT_TRUE(logger.addSink(std::move(sink)));

    EXPECT_TRUE(logger.info("target = {}, confidence = {:.2f}", 3, 0.75));
    const auto records = sink_ptr->records();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records.front().level, rm::LogLevel::Info);
    EXPECT_EQ(records.front().text, "target = 3, confidence = 0.75");
}

TEST(LOG_logger, fanout_calls_all_sinks_with_one_record) {
    rm::Logger logger;
    auto first = std::make_unique<RecordingSink>();
    auto second = std::make_unique<RecordingSink>();
    auto *first_ptr = first.get();
    auto *second_ptr = second.get();
    ASSERT_TRUE(logger.addSink(std::move(first)));
    ASSERT_TRUE(logger.addSink(std::move(second)));

    EXPECT_TRUE(logger.warning("warning {}", 7));
    const auto first_records = first_ptr->records();
    const auto second_records = second_ptr->records();
    ASSERT_EQ(first_records.size(), 1);
    ASSERT_EQ(second_records.size(), 1);
    EXPECT_EQ(first_records.front().timestamp, second_records.front().timestamp);
    EXPECT_EQ(first_records.front().level, second_records.front().level);
    EXPECT_EQ(first_records.front().text, second_records.front().text);
}

TEST(LOG_logger, failed_sink_does_not_stop_fanout) {
    rm::Logger logger;
    auto successful = std::make_unique<RecordingSink>();
    auto failed = std::make_unique<RecordingSink>(false);
    auto trailing = std::make_unique<RecordingSink>();
    auto *successful_ptr = successful.get();
    auto *failed_ptr = failed.get();
    auto *trailing_ptr = trailing.get();
    ASSERT_TRUE(logger.addSink(std::move(successful)));
    ASSERT_TRUE(logger.addSink(std::move(failed)));
    ASSERT_TRUE(logger.addSink(std::move(trailing)));

    EXPECT_FALSE(logger.error("failure {}", 1));
    EXPECT_EQ(successful_ptr->records().size(), 1);
    EXPECT_EQ(failed_ptr->records().size(), 1);
    EXPECT_EQ(trailing_ptr->records().size(), 1);
}

TEST(LOG_logger, levels_are_preserved) {
    rm::Logger logger;
    auto sink = std::make_unique<RecordingSink>();
    auto *sink_ptr = sink.get();
    ASSERT_TRUE(logger.addSink(std::move(sink)));

    EXPECT_TRUE(logger.debug("debug"));
    EXPECT_TRUE(logger.info("info"));
    EXPECT_TRUE(logger.warning("warning"));
    EXPECT_TRUE(logger.error("error"));
    const auto records = sink_ptr->records();
    ASSERT_EQ(records.size(), 4);
    EXPECT_EQ(records[0].level, rm::LogLevel::Debug);
    EXPECT_EQ(records[1].level, rm::LogLevel::Info);
    EXPECT_EQ(records[2].level, rm::LogLevel::Warning);
    EXPECT_EQ(records[3].level, rm::LogLevel::Error);
}

TEST(LOG_logger, concurrent_writes_are_complete) {
    rm::Logger logger;
    auto sink = std::make_unique<RecordingSink>();
    auto *sink_ptr = sink.get();
    ASSERT_TRUE(logger.addSink(std::move(sink)));

    constexpr int thread_count = 8;
    constexpr int record_count = 100;
    std::vector<std::thread> threads;
    for (int thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&logger, thread]() {
            for (int index = 0; index < record_count; ++index)
                EXPECT_TRUE(logger.info("thread {} record {}", thread, index));
        });
    }
    for (auto &thread : threads)
        thread.join();

    const auto records = sink_ptr->records();
    ASSERT_EQ(records.size(), thread_count * record_count);
    for (const auto &record : records)
        EXPECT_NE(record.text.find("thread "), std::string::npos);
}

TEST(LOG_console_sink, writes_unified_text) {
    std::ostringstream stream;
    rm::ConsoleSink sink(stream);
    const rm::LogRecord record{std::chrono::system_clock::from_time_t(0) + std::chrono::milliseconds(123), rm::LogLevel::Error, "console"};
    EXPECT_TRUE(sink.write(record));
    EXPECT_EQ(stream.str(), rm::formatLogRecord(record) + "\n");
}

TEST(LOG_file_sink, creates_and_rotates_date_files) {
    const auto directory = makeTempDirectory("rotation");
    const auto now = std::chrono::system_clock::now();
    const auto later = now + std::chrono::hours(48);
    rm::FileSink sink(directory, 7);
    const rm::LogRecord first{now, rm::LogLevel::Info, "first"};
    const rm::LogRecord second{later, rm::LogLevel::Warning, "second"};
    ASSERT_TRUE(sink.write(first));
    ASSERT_TRUE(sink.write(second));
    EXPECT_TRUE(std::filesystem::exists(directory / (dateOf(now) + ".log")));
    EXPECT_TRUE(std::filesystem::exists(directory / (dateOf(later) + ".log")));
    std::filesystem::remove_all(directory);
}

TEST(LOG_file_sink, deletes_only_expired_managed_files) {
    const auto directory = makeTempDirectory("retention");
    const auto now = std::chrono::system_clock::now();
    const auto expired = dateOf(now - std::chrono::hours(72)) + ".log";
    const auto retained = dateOf(now - std::chrono::hours(24)) + ".log";
    std::ofstream(directory / expired) << "expired";
    std::ofstream(directory / retained) << "retained";
    std::ofstream(directory / "unrelated.log") << "unrelated";
    std::ofstream(directory / "notes.txt") << "notes";
    std::filesystem::create_directory(directory / "unrelated-directory");

    rm::FileSink sink(directory, 1);
    EXPECT_FALSE(std::filesystem::exists(directory / expired));
    EXPECT_TRUE(std::filesystem::exists(directory / retained));
    EXPECT_TRUE(std::filesystem::exists(directory / "unrelated.log"));
    EXPECT_TRUE(std::filesystem::exists(directory / "notes.txt"));
    EXPECT_TRUE(std::filesystem::exists(directory / "unrelated-directory"));
    std::filesystem::remove_all(directory);
}

TEST(LOG_file_sink, reports_open_failure) {
    const auto directory = makeTempDirectory("failure");
    const auto blocker = directory / "not-a-directory";
    std::ofstream(blocker) << "blocker";
    rm::FileSink sink(blocker / "logs");
    const rm::LogRecord record{std::chrono::system_clock::now(), rm::LogLevel::Error, "cannot write"};
    EXPECT_FALSE(sink.write(record));
    std::filesystem::remove_all(directory);
}

} // namespace rm_test
