#include "python_bindings/py_util/logging.h"

#include "python_bindings/binding_core.h"

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "core/util/logger.h"

namespace {
namespace nb = nanobind;

constexpr int kPyTraceLevel = 5;

class PythonSink : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
public:
    void RegisterLogger(nb::object logger) {
        std::unique_lock lock(logger_mutex_);
        logger_ = std::move(logger);
    }

    void UnregisterLogger() {
        std::unique_lock lock(logger_mutex_);
        logger_ = nb::object();
    }

protected:
    void sink_it_(spdlog::details::log_msg const& msg) override {
        nb::gil_scoped_acquire gil;
        if (!gil.is_valid()) {
            return;
        }

        nb::object target_logger;
        {
            std::shared_lock lock(logger_mutex_);
            target_logger = logger_;
        }
        if (!target_logger.is_valid()) {
            return;
        }

        static std::unordered_map<spdlog::level::level_enum, int> const kLevelMap{
                {spdlog::level::trace, kPyTraceLevel},
                {spdlog::level::debug, 10},
                {spdlog::level::info, 20},
                {spdlog::level::warn, 30},
                {spdlog::level::err, 40},
                {spdlog::level::critical, 50},
                {spdlog::level::off, 0}};

        int msg_py_level = 0;
        auto map_it = kLevelMap.find(msg.level);
        if (map_it != kLevelMap.end()) {
            msg_py_level = map_it->second;
        }

        if (!ShouldLog(target_logger, msg_py_level)) {
            return;
        }
        nb::str payload(msg.payload.data(), msg.payload.size());
        (void)target_logger.attr("log")(msg_py_level, payload);
    }

    void flush_() override {}

private:
    static bool ShouldLog(nb::object const& logger, int level) {
        return nb::cast<bool>(logger.attr("isEnabledFor")(level));
    }

    std::shared_mutex logger_mutex_;
    nb::object logger_;
};

std::shared_ptr<PythonSink> GetGlobalPythonSink() {
    static auto sink = std::make_shared<PythonSink>();
    return sink;
}

void SetupLoggingBridge() {
    nb::gil_scoped_acquire gil;
    if (!gil.is_valid()) {
        return;
    }
    try {
        nb::module_ logging = nb::module_::import_("logging");

        if (!nb::hasattr(logging, "TRACE")) {
            logging.attr("addLevelName")(kPyTraceLevel, "TRACE");
            logging.attr("TRACE") = nb::int_(kPyTraceLevel);
        }

        nb::object py_logger = logging.attr("getLogger")("desbordante");

        auto python_sink = GetGlobalPythonSink();
        python_sink->RegisterLogger(std::move(py_logger));

        ::util::logging::Initialize({std::move(python_sink)});
    } catch (nb::python_error const& e) {
        std::string message = "ERROR: Error during Python logging setup: ";
        message += e.what();
        nb::module_ sys = nb::module_::import_("sys");
        nb::print(message.c_str(), nb::handle(), sys.attr("stderr"));
        throw;
    }
}

void CleanupAtExit() {
    if (auto sink = GetGlobalPythonSink()) {
        sink->UnregisterLogger();
    }
}

}  // namespace

namespace python_bindings {

void BindLogging(nb::module_&) {
    SetupLoggingBridge();
    nb::module_::import_("atexit").attr("register")(nb::cpp_function(CleanupAtExit));
}

}  // namespace python_bindings
