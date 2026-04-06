#include <gtest/gtest.h>
#include "vehicle-sim/TelemetryFormatter.h"
#include "vehicle-sim/domain/TelemetrySignal.h"

using namespace vehicle_sim;

// ================================================
// TelemetryFormatter Unit Tests
// TDD - Tests for data formatting logic
// ================================================

TEST(TelemetryFormatterTest, FormatsToJSON)
{
    TelemetryFormatter formatter(Format::JSON);

    domain::TelemetrySignal data{
        3000.0,      // rpm
        3,            // gear
        450.0,        // torqueNm
        100.0,        // speedKmh
        50.0,         // throttlePercent
        1500          // timestampUtcMs
    };

    std::string result = formatter.format(data);

    // Verify JSON format (basic checks)
    EXPECT_TRUE(result.find("\"timestamp\":1500") != std::string::npos);
    EXPECT_TRUE(result.find("\"rpm\":3000") != std::string::npos);
    EXPECT_TRUE(result.find("\"speed\":100") != std::string::npos);
    EXPECT_TRUE(result.find("\"throttle\":50") != std::string::npos);
    EXPECT_TRUE(result.find("\"torque\":450") != std::string::npos);
    EXPECT_TRUE(result.find("\"gear\":3") != std::string::npos);
}

TEST(TelemetryFormatterTest, FormatsToCSVWithHeaders)
{
    TelemetryFormatter formatter(Format::CSV);

    domain::TelemetrySignal data{
        2500.0,      // rpm
        2,            // gear
        300.0,        // torqueNm
        80.0,         // speedKmh
        40.0,         // throttlePercent
        1000          // timestampUtcMs
    };

    std::string result = formatter.format(data);

    // Verify CSV format with headers
    EXPECT_TRUE(result.find("timestamp,rpm,speed,throttle,torque,gear") != std::string::npos);
    // Check that key values are present (avoiding fragile exact string matching)
    EXPECT_TRUE(result.find("1000") != std::string::npos); // timestamp
    EXPECT_TRUE(result.find("2500.") != std::string::npos); // rpm
    EXPECT_TRUE(result.find("80.") != std::string::npos); // speed
}

TEST(TelemetryFormatterTest, FormatsToCSVWithoutHeaders)
{
    TelemetryFormatter formatter(Format::CSV);
    formatter.setIncludeHeaders(false);

    domain::TelemetrySignal data{
        2500.0,      // rpm
        2,            // gear
        300.0,        // torqueNm
        80.0,         // speedKmh
        40.0,         // throttlePercent
        1000          // timestampUtcMs
    };

    std::string result = formatter.format(data);

    // Verify CSV format without headers
    EXPECT_FALSE(result.find("timestamp,rpm") != std::string::npos);
    // Check that key values are present
    EXPECT_TRUE(result.find("1000") != std::string::npos);
    EXPECT_TRUE(result.find("2500.") != std::string::npos);
    EXPECT_TRUE(result.find("80.") != std::string::npos);
}

TEST(TelemetryFormatterTest, FormatsToPlainText)
{
    TelemetryFormatter formatter(Format::PLAINTEXT);

    domain::TelemetrySignal data{
        4000.0,      // rpm
        4,            // gear
        500.0,        // torqueNm
        120.0,        // speedKmh
        80.0,         // throttlePercent
        2000          // timestampUtcMs
    };

    std::string result = formatter.format(data);

    // Verify plain text format
    EXPECT_TRUE(result.find("Telemetry:") != std::string::npos);
    EXPECT_TRUE(result.find("Time: 2000 ms") != std::string::npos);
    EXPECT_TRUE(result.find("RPM: 4000") != std::string::npos);
    EXPECT_TRUE(result.find("Speed: 120 km/h") != std::string::npos);
    EXPECT_TRUE(result.find("Throttle: 80%") != std::string::npos);
    EXPECT_TRUE(result.find("Torque: 500 Nm") != std::string::npos);
    EXPECT_TRUE(result.find("Gear: 4") != std::string::npos);
}

TEST(TelemetryFormatterTest, UsesCustomDelimiter)
{
    TelemetryFormatter formatter(Format::CSV);
    formatter.setDelimiter(';');

    domain::TelemetrySignal data{
        2500.0,      // rpm
        2,            // gear
        300.0,        // torqueNm
        80.0,         // speedKmh
        40.0,         // throttlePercent
        1000          // timestampUtcMs
    };

    std::string result = formatter.format(data);

    // Verify custom delimiter is used
    // Check that custom semicolon appears in result
    size_t semicolonCount = 0;
    for (char c : result) {
        if (c == ';') semicolonCount++;
    }
    // Should have 5 semicolons (between 6 values)
    EXPECT_GE(semicolonCount, 5);
}

TEST(TelemetryFormatterTest, DefaultsToJSON)
{
    TelemetryFormatter formatter; // Default constructor

    domain::TelemetrySignal data{
        0, 0, 0, 0, 0, 0
    };

    std::string result = formatter.format(data);

    // Should format as JSON by default
    EXPECT_TRUE(result.find("{") != std::string::npos);
    EXPECT_TRUE(result.find("}") != std::string::npos);
}

TEST(TelemetryFormatterTest, ChangesFormat)
{
    TelemetryFormatter formatter(Format::CSV);
    formatter.setIncludeHeaders(false);

    domain::TelemetrySignal data{0, 0, 0, 0, 0, 0};

    std::string csvResult = formatter.format(data);
    EXPECT_TRUE(csvResult.find("{") == std::string::npos); // Not JSON

    formatter.setFormat(Format::JSON);
    std::string jsonResult = formatter.format(data);
    EXPECT_TRUE(jsonResult.find("{") != std::string::npos); // Is JSON
}

TEST(TelemetryFormatterTest, HandlesZeroValues)
{
    TelemetryFormatter formatter(Format::JSON);

    domain::TelemetrySignal data{0, 0, 0, 0, 0, 0};

    std::string result = formatter.format(data);

    // Should handle zeros correctly
    EXPECT_TRUE(result.find("\"timestamp\":0") != std::string::npos);
    EXPECT_TRUE(result.find("\"rpm\":0") != std::string::npos);
    EXPECT_TRUE(result.find("\"speed\":0") != std::string::npos);
}
